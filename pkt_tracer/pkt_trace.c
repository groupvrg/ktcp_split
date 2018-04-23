#include <linux/init.h>      // included for __init and __exit macros
#include <linux/module.h>      // included for __init and __exit macros
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <net/sock.h>  //sock->to
#include "cbn_common.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Markuze Alex");
MODULE_DESCRIPTION("logging packets");

static unsigned int udp_port(struct sk_buff *skb)
{
	struct udphdr *udphdr = (struct udphdr *)skb_transport_header(skb);
	return ntohs(udphdr->dest);
}

static unsigned int tcp_port(struct sk_buff *skb)
{
	struct tcphdr *tcphdr = (struct tcphdr *)skb_transport_header(skb);
	return ntohs(tcphdr->dest);
}

static unsigned int get_port(struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	int port = 0;

	
	if (iph->protocol == 6)
		port = tcp_port(skb);	
	
	if (iph->protocol == 17)
		port = udp_port(skb);	
	return (port >=5000 && port <=6000);
}

static unsigned int cbn_trace_hook(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	if (skb->mark || get_port(skb)) {
		trace_only(skb, priv);
	}
	return NF_ACCEPT;
}

/*
static unsigned int cbn_ingress_hook(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	if (!skb->mark)
		goto out;
	if (trace_iph(skb, priv)) {
		struct iphdr *iphdr = ip_hdr(skb);
		struct tcphdr *tcphdr = (struct tcphdr *)skb_transport_header(skb);
		struct addresses *addresses;

		if (strcmp(priv, "RX"))
			goto out;

		addresses = kmem_cache_alloc(syn_slab, GFP_ATOMIC);
		if (unlikely(!addresses)) {
			pr_err("Faield to alloc mem %s\n", __FUNCTION__);
			goto out;
		}

		addresses->dest.sin_addr.s_addr	= iphdr->daddr;
		addresses->src.sin_addr.s_addr	= iphdr->saddr;
		addresses->dest.sin_port	= tcphdr->dest;
		addresses->src.sin_port		= tcphdr->source;
		addresses->mark			= skb->mark;
		TRACE_PRINT("%s scheduling start_new_connection_syn [%lx]", __FUNCTION__, next_hop_ip);
		if (next_hop_ip)
			kthread_pool_run(&cbn_pool, start_new_pre_connection_syn, addresses); //elem?
		else
			kthread_pool_run(&cbn_pool, start_new_connection_syn, addresses); //elem?
		//1.alloc task + data
		//2.rb_tree lookup
		// sched poll on qp init - play with niceness?
	}

out:
	return NF_ACCEPT;
}
*/
#define CBN_PRIO_OFFSET 50

static struct nf_hook_ops cbn_nf_hooks[] = {
		{
		.hook		= cbn_trace_hook,
		.hooknum	= NF_INET_POST_ROUTING,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_FIRST,
		.priv		= "POST"
		},
		{
		.hook		= cbn_trace_hook,
		.hooknum	= NF_INET_POST_ROUTING,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_LAST,
		.priv		= "POST LAST"
		},
		{
		.hook		= cbn_trace_hook,
		.hooknum	= NF_INET_LOCAL_OUT,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_FIRST,
		.priv		= "NF_INET_LOCAL_OUT"
		},
		{
		.hook		= cbn_trace_hook,
		.hooknum	= NF_INET_LOCAL_IN,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_FIRST,
		.priv		= "NF_INET_LOCAL_IN"
		},
		{
		.hook		= cbn_trace_hook,
		.hooknum	= NF_INET_FORWARD,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_FIRST,
		.priv		= "NF_INET_FORWARD"
		},
		{
		.hook		= cbn_trace_hook,
		.hooknum	= NF_INET_PRE_ROUTING,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_FIRST,
		.priv		= "RX"
		},
		{
		.hook		= cbn_trace_hook,
		.hooknum	= NF_INET_PRE_ROUTING,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_LAST,
		.priv		= "RX-Last"
		},
//TODO: Add LOCAL_IN to mark packets with tennant_id
};

int __init pkt_trace_init(void)
{
	nf_register_net_hooks(&init_net, cbn_nf_hooks, ARRAY_SIZE(cbn_nf_hooks));
	return 0;
}

void __exit pkt_trace_clean(void)
{
	nf_unregister_net_hooks(&init_net, cbn_nf_hooks,  ARRAY_SIZE(cbn_nf_hooks));
}

module_init(pkt_trace_init);
module_exit(pkt_trace_clean);


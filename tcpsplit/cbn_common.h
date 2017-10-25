#ifndef __CBN_COMMON_H__
#define __CBN_COMMON_H__

#include <linux/kernel.h>
#include <linux/printk.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/udp.h>
#include <uapi/linux/tcp.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

#define cbn_err(fmt, ...)	__cbn_err("%s: " fmt, __FUNCTION__, ##__VA_ARGS__);	\
				pr_err( "%s: " fmt, __FUNCTION__, ##__VA_ARGS__)

static inline void __cbn_err(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	trace_printk(fmt, args);
	va_end(args);
}

#define cbn_info(fmt, ...) trace_printk(fmt, ##__VA_ARGS__);
//__cbn_err(fmt, ##__VA_ARGS__)
//
static inline void hex_dump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (!(i % 16))
			cbn_info("\n%lu\t:", i);
		cbn_info("%02x ", *(buf + i));
	}
	cbn_info("\n");
}

static inline const char *iphdr_flag(uint16_t flags)
{
	if (flags & BIT(14))
		return "DF";
	if (flags & BIT(13))
		return "MF";
	return "";
}

static inline const char *proto_string(u8 protocol)
{
	switch(protocol) {

	case 1: return "ICMP";
	case 4: return "IP";
	case 6: return "TCP";
	case 17: return "UDP";
	case 50: return "ESP";
	case 51: return "AH";
	}
	return "";
}

#define dump_iph(iphdr) 	idx += sprintf(&store[idx],"\t\tmark %d secmark %d\n\t\t"  \
		 "ihl %d ver %d tos %d total len %d\n\t\t"							\
		 "id %05d frag_off %05lu [%s]\n\t\t"								\
		 "ttl %d proto %s[%d] csum 0x%x\n\t\t"								\
		 "saddr %pI4n \n\t\t"										\
		 "daddr %pI4n \n"										\
			,skb->mark, skb->secmark,								\
			iphdr->ihl, iphdr->version,								\
			iphdr->tos, ntohs(iphdr->tot_len),							\
			ntohs(iphdr->id), ntohs(iphdr->frag_off) & (BIT(13) -1),				\
			iphdr_flag(ntohs(iphdr->frag_off)),							\
			iphdr->ttl, proto_string(iphdr->protocol), iphdr->protocol,				\
			iphdr->check, &iphdr->saddr, &iphdr->daddr						\
			);
#define dump_tcph(tcphdr)	idx += sprintf(&store[idx], "\n %d => %d "					\
						"%s %s %s\n"								\
						"seq %d ack %d window %d\n"							\
						,ntohs(tcphdr->source), ntohs(tcphdr->dest)			\
						, tcphdr->syn ? "SYN" : ""					\
						, tcphdr->ack ? "ACK" : ""					\
						, tcphdr->ack ? "FIN" : ""					\
						,ntohl(tcphdr->seq), ntohl(tcphdr->ack_seq)			\
						,ntohs(tcphdr->window)						\
						);
static inline void trace_iph(struct sk_buff *skb, const char *str)
{
	struct iphdr *iphdr = ip_hdr(skb);
	int idx = 0;
	char store[512] = {0};

	dump_iph(iphdr);
	if (iphdr->protocol == 6) {
		struct tcphdr *tcphdr = (struct tcphdr *)skb_transport_header(skb);
		dump_tcph(tcphdr);
	}
	trace_printk(store);
	return;
/*
	idx += sprintf(&store[idx],"\n\t\t>>>>>>>>>>>>>%s>>>>>>>>>>>>>>>>\n",str);
	if (skb->mark == 166)
		idx += sprintf(&store[idx],"\n\t\tMARK SET\n");
	dump_iph(iphdr);

	if (iphdr->saddr == 0 || iphdr->daddr == 0) {
		idx += sprintf(&store[idx],"WARNING: d/saddr are 0\n");
	}
	idx += sprintf(&store[idx],"\t\t%d %s\n", skb->skb_iif, (skb->dev) ? skb->dev->name : "NetDev not set");

	if (iphdr->protocol == 17) {
		struct udphdr *udphdr = (struct udphdr *)skb_transport_header(skb);
		idx += sprintf(&store[idx],"\t\t%p %p:\n\t\t src : %05d  dst %05d\n"
				, udphdr, iphdr,
				ntohs(udphdr->source), ntohs(udphdr->dest));
		if (ntohs(udphdr->dest) == 4500) {
			skb->mark = 166;
		}
	}
	idx += sprintf(&store[idx],"\n\t\t=================\n");
	trace_printk(store);
	*/
}
#endif /*__CBN_COMMON_H__*/

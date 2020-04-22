#!/usr/bin/python

import os, sys
import subprocess
sys.path.append(os.path.dirname(os.path.realpath(__file__)) + "/../")
from cbn_tools.cbn_utils import *

jump_host = "35.198.147.38"


def get_default_route(branch_ssh_base, branch_ssh_end) :
        
        default_rt_cmd = " | awk '/default/ { print $3 }'"    
        default_route = subprocess.check_output(branch_ssh_base + "ip route" + branch_ssh_end + default_rt_cmd, shell=True)

        return default_route.split("\n")[0]

def cbn_branch_virt_branch_ipsec_setup(env_name, vir_branch_name, branch_name, branch_network, 
			               vm_intf_ip, cbn_gw_ip) :

        cust_ipsec_file_name = "/tmp/"+vir_branch_name+"_cbn_pop"
        cust_ipsec_conf_get_str = cbn_tenant_util(env_name) + " get-conf --name " + branch_name + \
				  " --ipsec-conf " + cust_ipsec_file_name 
        cust_ipsec_conf_file_name = "/tmp/"+vir_branch_name+"_cbn_pop-strongswan.conf"
        cust_ipsec_secrets_file_name = "/tmp/"+vir_branch_name+"_cbn_pop-strongswan.secrets"
 

	rc = os.system(cust_ipsec_conf_get_str) 
	if rc != 0 :
	    sys.exit(-1)

        rc = os.system("sed -i 's/==/=/g' " + cust_ipsec_conf_file_name)
	if rc != 0 :
	    sys.exit(-1)

        rc = os.system("sed -i 's/auto=route/auto=start/g' " + cust_ipsec_conf_file_name)
	if rc != 0 :
	    sys.exit(-1)

        rc = os.system("sed -i 's/mark=.*/mark=100/g' " + cust_ipsec_conf_file_name)
	if rc != 0 :
	    sys.exit(-1)

        rc = os.system("echo '  closeaction=restart ' >>  " + cust_ipsec_conf_file_name)
	if rc != 0 :
	    sys.exit(-1)

        cust_ipsec_conn_str = branch_name

        os_str_base = "ssh  -o loglevel=quiet -o StrictHostKeyChecking=no root@" + vm_intf_ip + " \""
        os_str_end = " \""

        ## Copy files over
        os_scp_str = "scp  -o loglevel=quiet -o StrictHostKeyChecking=no " + cust_ipsec_conf_file_name + " root@" + vm_intf_ip + ":/etc/ipsec.d/cbn_pop.conf"
	rc = os.system(os_scp_str) 
	if rc != 0 :
	    sys.exit(-1)

        os_scp_str = "scp  -o loglevel=quiet -o StrictHostKeyChecking=no " + cust_ipsec_secrets_file_name + " root@" + vm_intf_ip + ":/etc/ipsec.d/cbn_pop.secrets"
	rc = os.system(os_scp_str) 
	if rc != 0 :
	    sys.exit(-1)
        ipsec_str = os_str_base + "ipsec rereadall && ipsec reload && ipsec statusall " + os_str_end

        print vir_branch_name + " : Bringup IPSec tunnel " + cust_ipsec_conn_str
        
	rc = os.system(ipsec_str) 
	if rc != 0 :
	    sys.exit(-1)

        print vir_branch_name + " : Setting up Vti tunnel ipsec_tun "

        tunnel_str = os_str_base + "ip tunnel add ipsec_tun mode vti key 100 ttl 200 local any remote " + cbn_gw_ip + \
		     " && ip link set ipsec_tun up "  + os_str_end
	rc = os.system(tunnel_str) 
	if rc != 0 :
	    sys.exit(-1)


        print vir_branch_name + " : Setting up routing ....."

        print vir_branch_name + " : Setting up cbn Gw Access........"

        default_route = get_default_route(os_str_base, os_str_end) 
        
        ## Setup cbn Gw reachability through current default route
        jump_host_reach_cmd = "ip route add %s via %s" % (jump_host, default_route)
        ip_route_setup_str = os_str_base + jump_host_reach_cmd + os_str_end 
	rc = os.system(ip_route_setup_str) 
	if rc != 0 :
	    sys.exit(-1)

        print vir_branch_name + " : Setting up Jump Host Access........"

        ## Setup jump host reachability through current default route
        jump_host_reach_cmd = "ip route add %s via %s" % (cbn_gw_ip, default_route)
        ip_route_setup_str = os_str_base + jump_host_reach_cmd + os_str_end 
	rc = os.system(ip_route_setup_str) 
	if rc != 0 :
	    sys.exit(-1)

        print vir_branch_name + " : Setting up Internet and branch-to-branch access to go through cbn........"

        ## Setup default route reachability through ipsec tun
        default_reach_cmd = "ip route del default via %s ; ip route add default dev ipsec_tun" % (default_route)
        print default_reach_cmd
        ip_route_setup_str = os_str_base + default_reach_cmd + os_str_end 
	rc = os.system(ip_route_setup_str) 
	if rc != 0 :
	    sys.exit(-1)


def cbn_branch_virt_branch_ipsec_cleanup(branch_name, cbn_gw_ip): 


        os_str_base = "ssh  -o loglevel=quiet -o StrictHostKeyChecking=no root@" + vm_intf_ip + " \""
        os_str_end = " \""


        print cbn_gw_ip + " : Bringing down ipsec tunnel......"
        ipsec_str = os_str_base + "ipsec down " + branch_name +  os_str_end
	rc = os.system(ipsec_str) 
	if rc != 0 :
	    sys.exit(-1)






if (len(sys.argv) != 7 and len(sys.argv) != 8) \
   or (len(sys.argv) == 8 and sys.argv[7] != "cleanup")  :
        print "Usage : cbn_virt_branch_ipsec_setup <Env name> <Branch name> <Virtual Branch name> <Branch IP Network> <Branch Virtual Intf IP> <CBN Pop Gw IP> [cleanup]\n"
        sys.exit(-1)

env_name = sys.argv[1]
branch_name = sys.argv[2] 
vir_branch_name = sys.argv[3] 
branch_network = sys.argv[4]
vm_intf_ip = sys.argv[5]
cbn_gw_ip = sys.argv[6]


cleanup = False
if len(sys.argv) == 8 :
   cleanup = True

if cleanup :
    cbn_branch_virt_branch_ipsec_cleanup(branch_name, cbn_gw_ip)
else :
    cbn_branch_virt_branch_ipsec_setup(env_name, vir_branch_name, branch_name, branch_network, 
			               vm_intf_ip, cbn_gw_ip)
          
sys.exit(0)





#!/usr/bin/python

import os, sys
import subprocess

def get_git_root_path() :
    return (subprocess.check_output("git rev-parse --show-toplevel", shell=True).split()[0]) + "/cbn-intg-demo-infra"

def cbn_pop_gw_ipsec_cleanup(vir_branch_name, cbn_gw_ip, pem_file, gw_user_name) :
    os_ssh_base_str = "ssh  -o loglevel=quiet -o StrictHostKeyChecking=no -i " + pem_file + " " + gw_user_name + \
		      "@" + cbn_gw_ip + " \"sudo bash -c \\\" "

    os_str_end = " \\\"\""
 
    gw_ipsec_cleanup_str = os_ssh_base_str + "sed -i \'/conn " + vir_branch_name + \
                           "/,+3 d' /etc/ipsec.d/CBN-Cust.conf" \
			   " && ipsec reload "+ os_str_end
    print(cbn_gw_ip + " : Cleaning up IPSec config for %s" % vir_branch_name)

    rc = os.system(gw_ipsec_cleanup_str)
    if rc != 0 :
        sys.exit(-1)


def cbn_pop_gw_ipsec_setup(vir_branch_name, cust_mark_val, branch_network, cbn_gw_ip, 
                           pem_file, gw_user_name): 
        
        base_gw_ipsec_str = [
        "config setup",
        "        charondebug=\"knl 2\"",
        "",
	"conn %default",
	"                ikelifetime=60m",
	"                keylife=20m",
	"                rekeymargin=3m",
	"                keyingtries=1",
	"                keyexchange=ikev2",
	"                mobike=no",
	"                leftsubnet=0.0.0.0/0",
	"                leftauth=psk",
	"                auto=route",
	"                right=%any",
	"                rightauth=psk"]

        gw_ipsec_str = [ 
        "conn " + vir_branch_name,
        "                rightid=@@" + vir_branch_name, 
        "                rightsubnet=" + branch_network,
        "                mark="+cust_mark_val]
 
        os_ssh_base_str = "ssh  -o loglevel=quiet -o StrictHostKeyChecking=no -i " + pem_file + " " + gw_user_name + \
		      "@" + cbn_gw_ip  
        os_subprocess_str = os_ssh_base_str + " \" "
        os_subprocess_str_end = " \""
        os_str_base = os_ssh_base_str + " \"sudo bash -c \\\" "
        os_str_end = " \\\"\""
  
        ipsec_str = "" + os_str_base 
        cbn_pop_cfg_str = os_subprocess_str + "ls /etc/ipsec.d/CBN-Cust.secrets | wc -l; exit 0" + os_subprocess_str_end
        
	is_cbn_pop_cfg_present = subprocess.check_output(cbn_pop_cfg_str, shell=True) 

        if int(is_cbn_pop_cfg_present) == 0 :
            print("Creating /etc/ipsec.d/CBN-Cust.secrets")
            secrets_str = os_str_base + "echo ': PSK 123456789' >> /etc/ipsec.d/CBN-Cust.secrets " \
                           + "&& ipsec rereadsecrets" + os_str_end
	    rc = os.system(secrets_str)
	    if rc != 0 :
	        sys.exit(-1)

        cbn_pop_cfg_str = os_subprocess_str + "ls /etc/ipsec.d/CBN-Cust.conf | wc -l; exit 0" + os_subprocess_str_end
        
	is_cbn_pop_cfg_present = subprocess.check_output(cbn_pop_cfg_str, shell=True) 

        if int(is_cbn_pop_cfg_present) == 0 :
            print("Creating /etc/ipsec.d/CBN-Cust.conf")

            for each_str in base_gw_ipsec_str :
                ipsec_str += "echo '" + each_str + "' >> /etc/ipsec.d/CBN-Cust.conf && "


        for each_str in gw_ipsec_str :
             ipsec_str += "echo '" + each_str + "' >> /etc/ipsec.d/CBN-Cust.conf && "
    
        ipsec_str += " ipsec reload " + os_str_end

        print(cbn_gw_ip + " : Configuring IPSec for %s with mark %s " % (vir_branch_name, cust_mark_val))

        rc = os.system(ipsec_str)
	if rc != 0 :
	    sys.exit(-1)





if (len(sys.argv) != 6 and len(sys.argv) != 7) or sys.argv[5] not in ["aws", "gcp", "azure"]  \
   or (len(sys.argv) == 7 and sys.argv[6] != "cleanup") :
    print(
        "Usage : cbn_pop_gw_ipsec_setup <Virtual Branch name> <Virtual Branch Network> <Customer Vti Mark Value> <CBN pop Gw IP> <aws|gcp|azure> [cleanup]\n")
    sys.exit(-1)

cleanup = False
vir_branch_name = sys.argv[1] 
cust_mark_val = sys.argv[2] 
branch_network = sys.argv[3] 
cbn_gw_ip = sys.argv[4]
git_root_path = get_git_root_path()

if sys.argv[5] == "aws" :
        pem_file = git_root_path + "/cloud_access_keys/aws_master_key_pvt"
        gw_user_name = "ubuntu"

elif sys.argv[5] == "gcp" :
        pem_file = git_root_path + "/cloud_access_keys/gcp_master_key_pvt"
        gw_user_name = "ubuntu"

else :
        pem_file = git_root_path + "/cloud_access_keys/azure_master_key_pvt"
        gw_user_name = "ubuntu"


if len(sys.argv) == 7 :
    cleanup = True

if not cleanup :
	cbn_pop_gw_ipsec_setup(vir_branch_name, cust_mark_val, branch_network, cbn_gw_ip, 
        	               pem_file, gw_user_name) 
else :
	cbn_pop_gw_ipsec_cleanup(vir_branch_name, cbn_gw_ip, pem_file, gw_user_name)
          
sys.exit(0)





#!/bin/bash

sudo apt-get install git build-essential fakeroot libncurses5-dev libssl-dev ccache libelf-dev -y

source `dirname $0`/params.txt

grep -q "12 to_tun" /etc/iproute2/rt_tables
[ "$?" -eq  1 ]  && sudo bash -c 'echo 12 to_tun >> /etc/iproute2/rt_tables'

scripts=~/ENV/cbn-agents/scripts/utils
gue_port=5555
#dip=10.154.0.6
#sip=10.154.0.4
#dest=10.154.0.7

cd ~/ENV/cbn-agents/agents/datapath/fou/
make
./load.sh

sudo $scripts/disable_rpfilter.sh
sudo $scripts/enable_ipforwarding.sh

sudo ip fou add port $gue_port gue

sudo ip link add name gue type ipip \
                        remote $NEXT local $LOCAL \
                        encap gue encap-sport auto encap-dport $gue_port

sudo ip link set up gue
sudo ip rule add fwmark 2 table to_tun
sudo ip route add $SINK/32 dev gue table to_tun

uid=$UID

sudo iptables -A OUTPUT -t mangle -m owner --uid-owner $uid -p tcp -j MARK --set-mark 2


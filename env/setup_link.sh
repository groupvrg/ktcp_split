#!/bin/bash

ktcp_dev=~/ktcp_split/tcpsplit

[ -d "$ktcp_dev" ] && KTCP="$ktcp_dev"
[ -z "$1" ] || KTCP="$1"/ktcp_split/tcpsplit
[ -z "$KTCP" ] && KTCP=~/ENV/cbn-agents/agents/ktcp/tcpsplit/

sudo apt-get install git build-essential fakeroot libncurses5-dev libssl-dev ccache libelf-dev -y

source `dirname $0`/params.txt

grep -q "12 to_tun" /etc/iproute2/rt_tables
[ "$?" -eq  1 ]  && sudo bash -c 'echo 12 to_tun >> /etc/iproute2/rt_tables'

scripts=~/ENV/cbn-agents/scripts/utils
gue_port=5555

sudo $scripts/disable_rpfilter.sh

sudo $scripts/enable_ipforwarding.sh

cd ~/ENV/cbn-agents/agents/datapath/fou/
make clean
make
./load.sh

cd $KTCP
./install.sh

sudo ip fou add port $gue_port gue

sudo ip link add name gueright type ipip \
                        remote $NEXT local $LOCAL \
                        encap gue encap-sport auto encap-dport $gue_port

sudo ip link add name gueleft type ipip \
                        remote $PREV local $LOCAL \
                        encap gue encap-sport auto encap-dport $gue_port

sudo ip link set up gueleft
sudo ip link set up gueright
sudo ip addr add dev gueleft $LOCAL
sudo ip addr add dev gueright $LOCAL

sudo ip rule add fwmark 2 table to_tun
sudo ip route add $SRC/32 dev gueleft table to_tun
sudo ip route add $SINK/32 dev gueright table to_tun


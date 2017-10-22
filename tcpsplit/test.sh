make
sudo insmod cbn_split.ko
sudo iptables -t nat -N SOCKS
sudo iptables -t nat -A SOCKS -p tcp -j REDIRECT --to-ports 9216 #--dport 5556
sudo iptables -t nat -A OUTPUT -p tcp -m owner --gid-owner sox -j SOCKS
#sudo -g sox netperf -H 10.154.0.6

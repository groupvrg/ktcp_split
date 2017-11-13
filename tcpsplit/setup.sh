#sudo ip link add name pipe type ipip remote 10.0.2.4 local 10.0.2.15
#sudo ip addr add 10.0.2.15 dev pipe
#sudo ip link set up dev pipe
sudo iptables -I PREROUTING -i ipip_a -t raw -j MARK --set-mark 10
sudo iptables -I PREROUTING -i ipip_b -t raw -j MARK --set-mark 10
sudo iptables -I PREROUTING -t nat  -p tcp -m mark --mark 10 -j REDIRECT --to-port 9216
#sudo iptables -I PREROUTING -i pipe -t nat  -p tcp -j REDIRECT --to-port 9216
#sudo iptables -I PREROUTING -t nat  -p tcp -j REDIRECT --to-port 9216
sudo sysctl -w net.ipv4.ip_forward=1
sudo sh -c "echo 0 > /proc/sys/net/ipv4/conf/all/rp_filter"

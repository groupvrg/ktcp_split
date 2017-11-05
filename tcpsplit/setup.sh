sudo ip link add name pipe type ipip remote 10.0.2.4 local 10.0.2.15
sudo ip link set up dev pipe
sudo iptables -I PREROUTING -i pipe -t raw -j MARK --set-mark 10
#sudo iptables -I PREROUTING -i pipe -t nat  -p tcp -m mark --mark 10 -j REDIRECT --to-port 9216
sudo iptables -I PREROUTING -i pipe -t nat  -p tcp -j REDIRECT --to-port 9216

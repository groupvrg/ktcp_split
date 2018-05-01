sudo iptables -I PREROUTING -t nat  -p tcp -m mark --mark 2 -j REDIRECT --to-port 5557

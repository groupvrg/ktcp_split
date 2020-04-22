#/bin/bash

#sudo ip route add 11.0.0.0/8 dev gueleft table to_tun
for i in `seq 0 255`; do
	        sudo ip route add 11.$i.0.0/16 dev gueleft table to_tun
done


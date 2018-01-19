sudo rmmod cbn_split
make
if [ "$?" != 0 ]; then
	exit
fi
sudo insmod cbn_split.ko
#sudo iptables -t nat -N SOCKS
#sudo iptables -t nat -A SOCKS -p tcp -j REDIRECT --to-ports 9216 #--dport 5556
#sudo iptables -t nat -A SOCKS -p tcp -j MARK --set-mark 10
#sudo iptables -t nat -A SOCKS -p tcp -j TPROXY --on-port 9216 --on-ip 127.0.0.1 #--dport 5556
#sudo iptables -t nat -A OUTPUT -p tcp -m owner --gid-owner sox -j SOCKS
#sudo iptables -t nat -A PREROUTING -p tcp -m owner --gid-owner sox -j SOCKS
sudo sh -c 'echo 0 > /proc/sys/kernel/hung_task_timeout_secs'
#sudo -g sox netperf -H 10.154.0.6
echo 10,12345 > /proc/cbn/cbn_proc
#echo 127,0,0,1 > /proc/cbn/conn_pool

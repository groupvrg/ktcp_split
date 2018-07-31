#sudo rmmod cbn_split
make
if [ "$?" != 0 ]; then
	RED='\033[1;31m'
	NC='\033[0m' # No Color
	echo
	echo -e "${RED}Please make sure this kernel has the cbn conntrack patches!!!${NC}"
	echo
	exit
fi

if [ -n "$1" ]; then
	sudo insmod cbn_split.ko pool_size=$1
else
	sudo insmod cbn_split.ko
fi

sudo sh -c 'echo 0 > /proc/sys/kernel/hung_task_timeout_secs'
sudo sh -c 'echo 1 > /proc/sys/kernel/ftrace_dump_on_oops'

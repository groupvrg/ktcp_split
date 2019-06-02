
sed -i "s/KTCP_VERSION/`git rev-parse --short HEAD`/g" split.c

make
git checkout split.c
if [ "$?" != 0 ]; then
	RED='\033[1;31m'
	NC='\033[0m' # No Color
	echo
	echo -e "${RED}Please make sure this kernel has the cbn conntrack patches!!!${NC}"
	echo
	exit
fi

pool_size=512

if [ -n "$1" ]; then
	pool_size=$1
fi

sudo insmod cbn_split.ko pool_size=$pool_size

sudo sh -c 'echo 0 > /proc/sys/kernel/hung_task_timeout_secs'
sudo sh -c 'echo 1 > /proc/sys/kernel/ftrace_dump_on_oops'

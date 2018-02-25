sudo rmmod cbn_split
make
if [ "$?" != 0 ]; then
	exit
fi
sudo insmod cbn_split.ko
sudo sh -c 'echo 0 > /proc/sys/kernel/hung_task_timeout_secs'
sudo sh -c 'echo 1 > /proc/sys/kernel/ftrace_dump_on_oops'
#echo 10,12345 > /proc/cbn/cbn_proc

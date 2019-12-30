
sudo insmod client.ko
sudo sh -c "echo 16 > /sys/kernel/debug/tracing/buffer_size_kb"
#echo 1 > /proc/io_client/tcp_client

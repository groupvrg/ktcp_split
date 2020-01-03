
sudo insmod client.ko
#sudo su
#cd /sys/kernel/debug/tracing
#echo 16 > buffer_size_kb
#echo trace_sendmsg >  set_graph_function
#echo function_graph > current_tracer
sudo sh -c "echo 16 > /sys/kernel/debug/tracing/buffer_size_kb"
#sudo sh -c "echo trace_sendmsg > /sys/kernel/debug/tracing/set_graph_function"
#sudo sh -c "echo function_graph > /sys/kernel/debug/tracing/current_tracer"
echo 0 > /proc/io_client/tcp_client

obj-m := pkt_trace.o
#cbn_split-y := split.o pool.o proc.o conn_pool.o

all:
	make -C /lib/modules/`uname -r`/build M=$(PWD) modules

clean:
	make -C /lib/modules/`uname -r`/build M=$(PWD) clean


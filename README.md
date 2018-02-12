# Instructions

To add a listening server, write
`echo <tid>,<port> > /proc/cbn/cbn_proc`

For example, to listen on port 5005 with mark = 10 do
`echo 10,5005 > /proc/cbn/cbn_proc`

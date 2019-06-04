links=(ew2b-mark-mc-2 ew2b-mark-mc-1)

for link in ${links[@]}; do
	echo $link
	#	rsync -re ssh -i ~/CBN_GCP_MASTER_KEY . $link:/ktcp_split/tcpsplit/
	ssh -i ~/CBN_GCP_MASTER_KEY $link rm -rf `pwd`/*
	ssh -i ~/CBN_GCP_MASTER_KEY $link rm -f .*
	rsync -vrue "ssh -i ~/CBN_GCP_MASTER_KEY " . $link:`pwd` &
done
wait

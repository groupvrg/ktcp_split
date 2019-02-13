links=(ew2b-mark-mc-2 ew2b-mark-mc-1)

for link in ${links[@]}; do
	echo $link
	#	rsync -re ssh -i ~/CBN_GCP_MASTER_KEY . $link:/ktcp_split/tcpsplit/
	rsync -vre "ssh -i ~/CBN_GCP_MASTER_KEY " . $link:`pwd`
done

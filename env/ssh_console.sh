#!/bin/bash

function usage {
	echo "usage : vm=<vm name on GCP> $0"
	echo "	optional params : key, project, zone"
}

[ "$key" ] || key=~/CBN_GCP_MASTER_KEY
[ "$project" ] || project='loyal-lore-152321'
[ "$zone" ] || zone='europe-west2-b'
#vm='ew2b-mark-mc-1'

[ "$vm" ] || usage
[ "$vm" ] || exit -1

ssh -i $key -p 9600 ${project}.${zone}.${vm}.${USER}@ssh-serialport.googleapis.com

GIT=https://xlr8vgn:fmPeuyzLR47y7ecJQTJV@github.com/groupvrg
ssh_opts=" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "
ssh $ssh_opts -i ${1} ${2} hostname
ssh $ssh_opts -i ${1} ${2} rm -rf ~/ENV;
ssh $ssh_opts -i ${1} ${2} mkdir -p ~/ENV;
scp $ssh_opts -r -i ${1} `dirname $0`/ENV/* ${2}:ENV/
scp $ssh_opts -r -i ${1} `dirname $0`/setup*sh ${2}:ENV/
scp $ssh_opts -r -i ${1} `dirname $0`/params_${3}.txt ${2}:ENV/params.txt
ssh $ssh_opts -i ${1} ${2} ls ~/ENV


#!/usr/local/bin/python3
##!/usr/bin/python3
import numpy as np
import matplotlib.pyplot as pl
from matplotlib import rcParams
rcParams['font.family'] = 'monospace'
rcParams['font.sans-serif'] = ['Helvetica']
from matplotlib.backends.backend_pdf import PdfPages
import pandas as pd
import argparse

fontsize=12
csv='user_vms.csv'
#parser = argparse.ArgumentParser(description='Generic plotter from 2 csvs')
#parser.add_argument('integers', metavar='N', type=int, nargs='+', help='an integer for the accumulator')
#parser.add_argument('exp', metavar='Experiment', help='name of experiment')
#parser.add_argument('-v', '--display', action='store_true', help='Display the graph')
#args = parser.parse_args()

vms = pd.read_csv(csv, index_col=0, sep=',');

#config spaces betwwen plots
fig = pl.figure(figsize=(21,16))
#pl.subplots_adjust(wspace=0.16, left=0.07, right=0.98, bottom=0.23, top=0.86)
pl.subplots_adjust(left=0.04, right=0.98, bottom=0.23, top=0.95)
pl.suptitle("#vms by user");

b1 = pl.bar(vms.index, vms['total']) 
b2 = pl.bar(vms.index, vms['old']) 
pl.xticks(vms.index, vms.user, fontsize=fontsize, rotation=-60)

list_t = vms['total'].tolist()
list_o = vms['old'].tolist()
for i in range(len(vms.index)):
	pl.text(x = i, y = list_t[i], s = list_t[i])
	if ((list_o[i] != 0) and (list_o[i] != list_t[i])):
		pl.text(x = i-0.2, y = list_o[i], s = list_o[i], color='orange')
pl.legend([b1,b2],labels=['Total VMs', 'VMs created before 2019'])
pl.grid(axis='y', linestyle=':')
pl.savefig('vms_by_user.pdf', format='pdf');
pl.show()

#def subplot(field, Text, divisor = 1, legend = True, text = True):
#	#working on plot 1
#	l1 = pl.plot(exp_l.index, exp_l[field]/divisor, marker='x', mew=2, markersize=10, label='Local', linewidth=3, color='purple') #linestyle='--', 
#	l2 = pl.plot(exp_r.index, exp_r[field]/divisor, marker='s', mew=2, markersize=10, label='Remote', linewidth=3 , fillstyle='none' , color='green')
#	print(field)
#	print(sum(exp_l[field])/len(exp_l[field]))
#	print(sum(exp_r[field])/len(exp_l[field]))
#	fig.text(0.5, 0.1, 'msg size', ha='center',fontsize=fontsize)
#	if (legend):
#		fig.legend([l1,l2],labels=['local/octo', 'remote'], loc='lower center', ncol=2, fontsize=fontsize)
#	#xtics n lables
#	pl.xticks(exp_l.index, exp_l.msg_size, fontsize=fontsize, rotation=0)
#	#pl.xlabel("Iteration")
#	pl.yticks(fontsize=fontsize)
#	pl.title(Text, fontsize=fontsize);
#	pl.grid(axis='y', linestyle=':')
#
#	list_l = exp_l[field].tolist()
#	list_r = exp_r[field].tolist()
#	lables = []
#
#	for i in range(len(exp_l.index)):
#		if  (list_r[i] > 0):
#			lables.append("%.2f" % (list_l[i]/list_r[i]))
#		else:
#			lables.append("nan")
#
#	if (text):
#		for i in range(len(exp_l.index)):
#		    pl.text(x = i +0.5, y = list_l[i]/divisor * 0.8 , s = lables[i] , size = 12, color='purple')
#			#if (i & 1):
#
#	pl.ylim(bottom=0, top=1.2 * max(max(list_l), max(list_r))/divisor)
#
#pl.subplot(121)
#avg_rr = sum(exp_l['avg_rr'].tolist())
#if (avg_rr != 0):
#	subplot('avg_rr', "Sockperf avg RR usec")
#elif (sum(exp_l['tps'].tolist()) != 0):
#	subplot('tps', "Transactions [KT/s]", 1000)
#else:
#	subplot('bandwidth', "throughput [Gb/s]")
##
#pl.subplot(122)
#pr = sum(exp_l['pr'].tolist())
#if (pr != 0):
#	subplot('pr', "Trial Time [s]")
#elif (avg_rr == 0):
#	subplot('sys_Memory', "Memory bandwidth [GB/s]", 1024.0)
#else:
#	subplot('std_rr', "avg RR stdev usec")

#fontsize=14
#x = ['ioct/local','','remote','','']
#netperf_time=[0,53.33,0,59.77,0]
#netperf_bw=[0,184.57,0,181.94,0]
#memc_time=[0,60.51,0,62.96,0]
#memc_tr=[0,16.56,0,14.36,0]
#
#x_pos = np.arange(5)
#
#fig, (ax1, ax2) = pl.subplots(1, 2, sharey=False, figsize=(6,2.5))
#pl.subplots_adjust(wspace=0.41, left=0.07, right=0.90, bottom=0.27, top=0.89)
#ax1.set_title('transactions [Kt/s]', fontsize=fontsize)
#ax1.bar(x_pos-.4, memc_tr, color='seagreen',tick_label=x)
#ax1.tick_params(labelsize=fontsize)
#ax1.locator_params(tight=True, nbins=4)
#ax1.grid(axis='y', linestyle=':')
#
#ax12 = ax1.twinx()
#ax12.bar(x_pos+0.4, memc_time,color='grey')
#ax12.tick_params(labelsize=fontsize)
#ax12.locator_params(tight=True, nbins=4)
#
#ax2.set_title('netpef [Gb/s]', fontsize=fontsize)
#ax2.bar(x_pos-.4, netperf_bw, color='blue',tick_label=x)
#ax2.tick_params(labelsize=fontsize)
#ax2.locator_params(tight=True, nbins=4)
#ax2.grid(axis='y', linestyle=':')
#
#ax22 = ax2.twinx()
#ax22.bar(x_pos+0.4, netperf_time,color='grey')
#ax22.tick_params(labelsize=fontsize)
#ax22.locator_params(tight=True, nbins=4)
#ax22.set_ylabel("time [s]", fontsize=fontsize);
#
#fig.legend([ax1,ax2,ax22],labels=['transactions', 'bandwidth', 'pr time'], loc='lower left', ncol=3, fontsize=fontsize,frameon=False)


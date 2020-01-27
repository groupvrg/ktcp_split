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
parser = argparse.ArgumentParser(description='Generic plotter from 2 csvs')
#parser.add_argument('integers', metavar='N', type=int, nargs='+', help='an integer for the accumulator')
parser.add_argument('exp', metavar='Experiment', help='name of experiment')
parser.add_argument('-v', '--display', action='store_true', help='Display the graph')
args = parser.parse_args()

exp_l = pd.read_csv(args.exp +'_l.csv', index_col=0, sep=',');
exp_r = pd.read_csv(args.exp +'_r.csv', index_col=0, sep=',');

#config spaces betwwen plots
fig = pl.figure(figsize=(6,3))
pl.subplots_adjust(wspace=0.16, left=0.07, right=0.98, bottom=0.23, top=0.86)
#pl.suptitle(args.exp.replace("_"," "));


def subplot(field, Text, divisor = 1, legend = True, text = True):
	#working on plot 1
	l1 = pl.plot(exp_l.index, exp_l[field]/divisor, marker='x', mew=2, markersize=10, label='Local', linewidth=3, color='purple') #linestyle='--', 
	l2 = pl.plot(exp_r.index, exp_r[field]/divisor, marker='s', mew=2, markersize=10, label='Remote', linewidth=3 , fillstyle='none' , color='green')
	print(field)
	print(sum(exp_l[field])/len(exp_l[field]))
	print(sum(exp_r[field])/len(exp_l[field]))
	fig.text(0.5, 0.1, 'msg size', ha='center',fontsize=fontsize)
	if (legend):
		fig.legend([l1,l2],labels=['local/octo', 'remote'], loc='lower center', ncol=2, fontsize=fontsize)
	#xtics n lables
	pl.xticks(exp_l.index, exp_l.msg_size, fontsize=fontsize, rotation=0)
	#pl.xlabel("Iteration")
	pl.yticks(fontsize=fontsize)
	pl.title(Text, fontsize=fontsize);
	pl.grid(axis='y', linestyle=':')

	list_l = exp_l[field].tolist()
	list_r = exp_r[field].tolist()
	lables = []

	for i in range(len(exp_l.index)):
		if  (list_r[i] > 0):
			lables.append("%.2f" % (list_l[i]/list_r[i]))
		else:
			lables.append("nan")

	if (text):
		for i in range(len(exp_l.index)):
		    pl.text(x = i +0.5, y = list_l[i]/divisor * 0.8 , s = lables[i] , size = 12, color='purple')
			#if (i & 1):

	pl.ylim(bottom=0, top=1.2 * max(max(list_l), max(list_r))/divisor)

pl.subplot(121)
avg_rr = sum(exp_l['avg_rr'].tolist())
if (avg_rr != 0):
	subplot('avg_rr', "Sockperf avg RR usec")
elif (sum(exp_l['tps'].tolist()) != 0):
	subplot('tps', "Transactions [KT/s]", 1000)
else:
	subplot('bandwidth', "throughput [Gb/s]")
#
pl.subplot(122)
pr = sum(exp_l['pr'].tolist())
if (pr != 0):
	subplot('pr', "Trial Time [s]")
elif (avg_rr == 0):
	subplot('sys_Memory', "Memory bandwidth [GB/s]", 1024.0)
else:
	subplot('std_rr', "avg RR stdev usec")

pl.savefig(args.exp + '.pdf', format='pdf');
if args.display:
	pl.show()


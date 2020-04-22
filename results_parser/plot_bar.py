#!/usr/local/bin/python3
import numpy as np
import matplotlib.pyplot as pl
from matplotlib.backends.backend_pdf import PdfPages
import pandas as pd
import argparse

fontsize=14
x = ['ioct/local','','remote','','']
netperf_time=[0,53.33,0,59.77,0]
netperf_bw=[0,184.57,0,181.94,0]
memc_time=[0,60.51,0,62.96,0]
memc_tr=[0,16.56,0,14.36,0]

x_pos = np.arange(5)

fig, (ax1, ax2) = pl.subplots(1, 2, sharey=False, figsize=(6,2.5))
pl.subplots_adjust(wspace=0.41, left=0.07, right=0.90, bottom=0.27, top=0.89)
ax1.set_title('transactions [Kt/s]', fontsize=fontsize)
ax1.bar(x_pos-.4, memc_tr, color='seagreen',tick_label=x)
ax1.tick_params(labelsize=fontsize)
ax1.locator_params(tight=True, nbins=4)
ax1.grid(axis='y', linestyle=':')

ax12 = ax1.twinx()
ax12.bar(x_pos+0.4, memc_time,color='grey')
ax12.tick_params(labelsize=fontsize)
ax12.locator_params(tight=True, nbins=4)

ax2.set_title('netpef [Gb/s]', fontsize=fontsize)
ax2.bar(x_pos-.4, netperf_bw, color='blue',tick_label=x)
ax2.tick_params(labelsize=fontsize)
ax2.locator_params(tight=True, nbins=4)
ax2.grid(axis='y', linestyle=':')

ax22 = ax2.twinx()
ax22.bar(x_pos+0.4, netperf_time,color='grey')
ax22.tick_params(labelsize=fontsize)
ax22.locator_params(tight=True, nbins=4)
ax22.set_ylabel("time [s]", fontsize=fontsize);

fig.legend([ax1,ax2,ax22],labels=['transactions', 'bandwidth', 'pr time'], loc='lower left', ncol=3, fontsize=fontsize,frameon=False)

pl.savefig('coloc' + '.pdf', format='pdf');
pl.show()

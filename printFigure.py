#!/usr/bin/env python
# encoding: utf-8

import matplotlib as mpl
import matplotlib.pyplot as plt
import sys

# %matplotlib inline

def main(fileName,output):
    cwnd = []
    ssthresh = []
    with open(fileName, 'r') as rf:
        lines = rf.readlines()
        for line in lines:
            if len(line) > 5 and line[0:4] == 'SEND':
                split = line.split()
                cwnd.append(int(split[3]))
                ssthresh.append(int(split[4]))
    fig = plt.figure()

    # plt.style.use('default')
    width = 345
    nice_fonts = {
            # Use LaTex to write all text
            "text.usetex": True,
            "font.family": "serif",
            # Use 10pt font in plots, to match 10pt font in document
            "axes.labelsize": 25,
            "font.size": 15,
            # Make the legend/label fonts a little smaller
            "legend.fontsize": 15,
            "xtick.labelsize": 15,
            "ytick.labelsize": 15,
    }

    mpl.rcParams.update(nice_fonts)
    color_set =  ["#9b59b6", "#3498db", "2ecc71", "#34495e"] 
    style_set = ['-', '--', 'dashdot',':']
    f = plt.figure(figsize=(6, 3))
    ax = f.add_subplot(1, 1, 1)
    # ax.set_prop_cycle(color=color_set, linestyle = style_set)
    x = range(len(ssthresh))
    plt.plot(x, cwnd, '-', label = 'cwnd')
    plt.plot(x, ssthresh, '--', label = 'ssthresh')
    plt.legend(loc = 'upper left')

    thre = 700
    if len(x) > 2 * thre:
        plt.savefig(output + "_1.pdf")
        print("Output figure 1 is saved as '" + output + "_1.pdf'")

        f = plt.figure(figsize=(6, 3))
        ax = f.add_subplot(1, 1, 1)
        # ax.set_prop_cycle(color=color_set, linestyle = style_set)
        x = range(len(ssthresh))
        plt.plot(x[0:thre], cwnd[0:thre], '-', label = 'cwnd')
        plt.plot(x[0:thre], ssthresh[0:thre], '--', label = 'ssthresh')
        plt.legend(loc = 'upper left')
        plt.savefig(output + "_2.pdf")
        print("Output figure 2 is saved as '" + output + "_2.pdf'")
    else:
        plt.savefig(output + ".pdf")
        print("Output figure is saved as '" + output + ".pdf'")


if __name__ == "__main__":
    if len(sys.argv) == 1:
        main('client.txt', 'print')
    elif len(sys.argv) == 2:
        main(sys.argv[1], 'print')
    else:
        if sys.argv[2][-4:] == '.pdf':
            fileName = sys.argv[2][:-4]
        else:
            fileName = sys.argv[2]
        main(sys.argv[1], fileName)

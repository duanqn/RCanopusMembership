import os
import argparse
import matplotlib
import openpyxl
import numpy as np
import scipy.stats
import matplotlib.pyplot as plt

column = {'aggregate sending rate': 0,
    'intra-bg latency': 1,
    'inter-bg latency': 2,
    'inter-bg bandwidth': 3,
    'bg': 4,
    'note': 5}

data_column = {'latency_avg': 7,
    'latency_5': 8,
    'latency_25': 9,
    'latency_50': 10,
    'latency_75': 11,
    'latency_95': 12,
    'throughput': 13}

data_enhance = []

def processData(data, confidence):
    arr = np.array(data)
    n = len(arr)
    mean, stderr = np.mean(arr), scipy.stats.sem(arr)
    h = stderr * scipy.stats.t.ppf((1 + confidence) / 2, n-1)
    
    return mean, h

def parseNextLine(lines, pos):
    begin = pos
    for i in range(begin, len(lines)):
        pos = i + 1
        line = lines[i].strip('\n').strip()
        if line.startswith('#'):
            continue
        if not line:
            continue

        if ':' in line:
            line = line.split(':')[-1].strip()
        break

    return line, pos

def parseKV(lines, pos):
    line, pos = parseNextLine(lines, pos)
    segs = line.split('=')
    key = segs[0].strip()
    value = segs[1].strip()

    return key, value, pos

def parseNumGraph(lines, pos):
    line, pos = parseNextLine(lines, pos)
    return int(line), pos

def parseGraph(lines, pos):
    filename, pos = parseNextLine(lines, pos)
    caption, pos = parseNextLine(lines, pos)
    xtype, xname, pos = parseKV(lines, pos)

    cond = {}
    line, pos = parseNextLine(lines, pos)
    numCond = int(line)
    if numCond < len(column) - 1:
        print("Warning: Not enough condition set")
    elif numCond > len(column) - 1:
        print("Warning: Too many conditions")

    data_candidate = data_enhance.copy()
    #print(data_candidate)
    for i in range(0, numCond):
        k, v, pos = parseKV(lines, pos)
        cond[k] = v
        if k != 'note':
            v = int(v)
        data_candidate = [data_entry for data_entry in data_candidate if data_entry[column[k]] == v]
    
    data_candidate = sorted(data_candidate, key=lambda entry: entry[column[xtype]])
    for data_candidate_entry in data_candidate:
        print(data_candidate_entry)

    line, pos = parseNextLine(lines, pos)
    numY = int(line)

    fig, ax = plt.subplots()
    y_axes = []
    hasThroughput = False
    for i in range(0, numY):
        ytype, yname, pos = parseKV(lines, pos)
        y_axes.append((ytype, yname))
        if ytype == 'throughput':
            hasThroughput = True

    hasAux = False
    if numY > 1 and hasThroughput:
        hasAux = True
        ax_aux = ax.twinx()

    for y_axis in y_axes:
        ytype = y_axis[0]
        yname = y_axis[1]
        data_x = []
        data_y = []
        data_yerr = []
        for i in range(0, len(data_candidate)):
            data_candidate_entry = data_candidate[i]
            x = data_candidate_entry[column[xtype]]
            if not x in data_x:
                data_x.append(x)
                runs = [entry for entry in data_candidate if entry[column[xtype]] == x]
                yarray = []
                for run in runs:
                    yarray.append(run[data_column[ytype]])
                avg, err = processData(yarray, 0.95)
                data_y.append(avg)
                data_yerr.append(err)

        if numY > 1 and ytype == 'throughput':
            ax_aux.errorbar(data_x, data_y, yerr=data_yerr, marker = '.', markersize = 6, capsize = 3, linestyle = '--', color = 'k', label = yname)
            ax_aux.set_ylabel(yname, fontsize=14)
        else:
            ax.errorbar(data_x, data_y, yerr=data_yerr, marker = '.', markersize = 6, capsize = 3, linestyle = '-', color = 'k', label = yname)
            ax.set_ylabel(yname, fontsize=14)
    
    ax.set_title(caption)
    ax.set_ylim(bottom = 0, top=150000)
    ax.set_xlabel(xname)

    if hasAux:
        ax_aux.set_ylim(bottom = 0, top=120000)
        lines, labels = ax.get_legend_handles_labels()
        lines2, labels2 = ax_aux.get_legend_handles_labels()
        ax.legend(lines + lines2, labels + labels2, loc=2)
    else:
        ax.legend(loc=2)
    plt.tight_layout()
    fig.savefig(filename)
    return pos
        

def main(args):
    with open(args.input, 'r') as fin:
        lines = fin.readlines()
    
    data = []
    # skip lines[0]
    for i in range(1, len(lines)):
        line = lines[i]
        if 'ERROR' in line:
            continue
        
        segs = line.split('|')
        for j in range(0, len(segs)):
            segs[j] = segs[j].strip('\n').strip()
        tag = segs[0]
        latency_avg = float(segs[1])
        latency_5 = float(segs[2])
        latency_25 = float(segs[3])
        latency_50 = float(segs[4])
        latency_75 = float(segs[5])
        latency_95 = float(segs[6])
        throughput = float(segs[7])
        time = float(segs[8])

        data.append([tag, latency_avg, latency_5, latency_25, latency_50, latency_75, latency_95, throughput, time])

    for data_entry in data:
        tag = data_entry[0]
        tagsplit = tag.split('-')
        sending_rate_per_sl = int(tagsplit[1])
        intra_bg_latency = int(tagsplit[3])
        inter_bg_latency = int(tagsplit[4])
        inter_bg_bandwidth = int(tagsplit[6])
        numbg = int(tagsplit[7].rstrip('bg'))
        if len(tagsplit) == 10: # no notes
            note = ''
            run = int(tagsplit[9])
        else:
            note = tagsplit[8]
            run = int(tagsplit[10])
        aggregate_sending_rate = sending_rate_per_sl * numbg * 4

        data_enhance.append([aggregate_sending_rate, intra_bg_latency, inter_bg_latency, inter_bg_bandwidth, numbg, note, run] + data_entry[1:])
    
    with open(args.script, 'r') as sin:
        script_lines = sin.readlines()
    
    numGraph, pos = parseNumGraph(script_lines, 0)
    print(str(numGraph) + ' graph(s)')
    for i in range(0, numGraph):
        pos = parseGraph(script_lines, pos)
    
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description = '')
    parser.add_argument('-f', '--analysis-file', type=str, dest='input', required=True)
    parser.add_argument('-s', '--script-file', type=str, dest='script', required=True)

    args = parser.parse_args()
    main(args)
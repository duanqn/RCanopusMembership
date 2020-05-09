import os
import argparse
import subprocess

def parse_temp(filename):
    lines = []
    BG_info = []
    SLlist = []
    with open(filename, 'r') as fin:
        lines = fin.readlines()

    pos = 1
    
    segs = lines[pos].split(' ')
    total_BG = int(segs[0].strip().strip('\n'))
    BG_failure = int(segs[1].strip().strip('\n'))

    pos += 1

    for bg in range(0, total_BG):
        segs = lines[pos].split(' ')
        SL_num = int(segs[0].strip().strip('\n'))
        SL_failure = int(segs[1].strip().strip('\n'))

        BG_info.append((SL_num, SL_failure))

        for sl in range(0, SL_num):
            SLlist.append((bg, sl))

    return BG_info, SLlist

def main():
    parser = argparse.ArgumentParser(description = '')
    parser.add_argument('-f', '--template-file', type=str, dest='filename', required=False, default='config.temp')
    parser.add_argument('-e', '--executable', type=str, dest='exe', required=False, default='membership_server')
    parser.add_argument('-c', '--common-prefix', type=str, dest='prefix', required=False, default='testdir')
    parser.add_argument('-l', '--log-prefix', type=str, dest='logprefix', required=False, default='run-log-')

    args = parser.parse_args()
    BGinfo, SLid = parse_temp(args.filename)

    for i in range(0, len(SLid)):
        dirname = args.prefix + str(i+1)
        subprocess.run(['rm', '-rf', dirname])
        subprocess.run(['mkdir', '-p', dirname])

        subprocess.run(['cp', args.exe, dirname])

        temp_file = 'test.conf.'+str(i+1)
        subprocess.run(['cp', args.filename, temp_file])
        with open(temp_file, 'a') as fout:
            fout.write(' '.join([str(SLid[i][0]), str(SLid[i][1])]))
            fout.write('\n')

        subprocess.run(['mv', temp_file, dirname + '/test.conf'])

        temp_file = 'run.sh.' + str(i+1)
        log_filename = args.logprefix + 'BG' + str(SLid[i][0]) + '-SL' + str(SLid[i][1]) + '.log'
        with open(temp_file, 'w') as fout:
            fout.write('#!/bin/bash\n')
            fout.write('./' + args.exe + ' BG' + str(SLid[i][0]) + ' SL' + str(SLid[i][1]) + ' > ' + log_filename + '\n')
        
        subprocess.run(['mv', temp_file, dirname + '/membership-run.sh'])


if __name__ == "__main__":
    main()

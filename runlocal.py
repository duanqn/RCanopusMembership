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
    parser.add_argument('-e', '--executable', type=str, dest='exe', required=False, default='membership-run.sh')
    parser.add_argument('-c', '--common-prefix', type=str, dest='prefix', required=False, default='testdir')

    args = parser.parse_args()
    BGinfo, SLid = parse_temp(args.filename)

    thisdir = os.path.dirname(os.path.abspath(__file__))

    for i in range(0, len(SLid)):
        dirname = args.prefix + str(i+1)
        
        os.chdir(os.path.join(thisdir, dirname))
        subprocess.run(['chmod', '+x', args.exe])
        subprocess.Popen(['./' + args.exe])



if __name__ == "__main__":
    main()

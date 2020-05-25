import os
import argparse
import subprocess
import time

helper_script = 'membership-run.sh'
TC_SCRIPT_NAME = 'tc-script.temp.sh'
tc_script = 'set-tc.sh'

def parse_temp(config):
    lines = []
    BG_info = []
    SLlist = []
    with open(config['template config file'], 'r') as fin:
        lines = fin.readlines()

    pos = 0
    
    segs = lines[pos].split(' ')
    total_BG = int(segs[0].strip().strip('\n'))
    BG_failure = int(segs[1].strip().strip('\n'))


    for bg in range(0, total_BG):
        pos += 1
        segs = lines[pos].split(' ')
        SL_num = int(segs[0].strip().strip('\n'))
        SL_failure = int(segs[1].strip().strip('\n'))

        BG_info.append((SL_num, SL_failure))

        for sl in range(0, SL_num):
            pos += 1
            SLlist.append((bg, sl, lines[pos].split(' ')[0].strip()))

    return BG_info, SLlist

def duplicate(config, BGinfo, SLid, run_dict):
    dirs = []
    for i in range(0, len(SLid)):
        dirname = config['deploy folder prefix'] + str(i+1)
        dirs.append(dirname)
        subprocess.run(['rm', '-rf', dirname])
        subprocess.run(['mkdir', '-p', dirname])

        subprocess.run(['cp', config['executable name'], dirname])

        temp_file = 'test.conf.'+str(i+1)
        with open(config['template config file'], 'r') as ftemp:
            temp_lines = ftemp.readlines()
        
        with open(temp_file, 'w') as fout:
            fout.write(str(run_dict['run_rate']) + '\n')
            fout.writelines(temp_lines)
            fout.write(' '.join([str(SLid[i][0]), str(SLid[i][1])]))
            fout.write('\n')

        subprocess.run(['mv', temp_file, os.path.join(dirname, 'test.conf')])

        temp_file = 'run.sh.' + str(i+1)
        log_filename = run_dict['run_tag'] + 'BG' + str(SLid[i][0]) + '-SL' + str(SLid[i][1]) + '.log'
        with open(temp_file, 'w') as fout:
            fout.write('#!/bin/bash\n')
            fout.write('./' + config['executable name'] + ' BG' + str(SLid[i][0]) + ' SL' + str(SLid[i][1]) + ' > ' + log_filename + ' &\n')
        
        subprocess.run(['mv', temp_file, os.path.join(dirname, helper_script)])

        genScript(config, SLid, i, TC_SCRIPT_NAME)
        subprocess.run(['mv', TC_SCRIPT_NAME, os.path.join(dirname, tc_script)])

    return dirs

def deploy(config, local_folders, SLlist, machine_list):
    subprocess.run(['bash', 'ensureRemoteDir.sh', machine_list, config['deploy dir'], config['username']])
    for i in range(0, len(local_folders)):
        full_deploy_path = os.path.join(config['deploy dir'], config['deploy folder prefix'] + str(i))
        subprocess.run(['scp', '-r', local_folders[i], config['username'] + '@' + SLlist[i][2] + ':' + full_deploy_path])

def delete(config, local_folders, SLlist):
    for i in range(0, len(local_folders)):
        full_deploy_path = os.path.join(config['deploy dir'], config['deploy folder prefix'] + str(i))
        subprocess.run(['bash', 'deleteRemoteDir.sh', config['username'], SLlist[i][2], full_deploy_path])

def start(config, SLlist):
    for i in range(0, len(SLlist)):
        full_deploy_path = os.path.join(config['deploy dir'], config['deploy folder prefix'] + str(i))
        subprocess.run(['bash', 'remoteStart.sh', config['username'], SLlist[i][2], full_deploy_path, helper_script])

def stop(config, SLlist):
    for i in range(0, len(SLlist)):
        subprocess.run(['bash', 'killRemoteProcess.sh', config['username'], SLlist[i][2], 'membership'])

def clearLocalDirs(folders):
    for folder in folders:
        subprocess.run(['rm', '-r', folder])

def collect(config, SLlist, run_dict):
    target_folder = run_dict['run_tag']
    subprocess.run(['rm', '-rf', target_folder])
    for i in range(0, len(SLlist)):
        full_deploy_path = os.path.join(config['deploy dir'], config['deploy folder prefix'] + str(i))
        full_log_path = os.path.join(full_deploy_path, '*.log')
        subprocess.run(['mkdir', '-p', target_folder])
        subprocess.run(['scp', config['username'] + '@' + SLlist[i][2] + ':' + full_log_path, target_folder])

def genScript(config, SLlist, SLid, output_name):
    sl = SLlist[SLid]
    with open(output_name, 'w') as tcout:
        tcout.write('#!/bin/bash\n')
        tcout.write('sudo tc qdisc add dev ' + config['network interface name'])
        tcout.write(' root handle 1: cbq avpkt 1000 bandwidth 10gbit\n')

        handle = 0
        for otherSL in SLlist:
            if sl[0] == otherSL[0] and sl[1] == otherSL[1]:
                continue
            handle += 1
            tcout.write('sudo tc class add dev ' + config['network interface name'])
            tcout.write(' parent 1: classid 1:' + str(handle))
            if sl[0] == otherSL[0]:
                # same BG
                tcout.write(' cbq rate 10gbit')
            else:
                # inter-BG link
                tcout.write(' cbq rate ' + config['inter-bg bandwidth in mbps'] + 'mbit')
            tcout.write(' allot 1500 prio 5 bounded isolated\n')    # I don't really understand these
            
            tcout.write('sudo tc filter add dev ' + config['network interface name'])
            tcout.write(' parent 1: protocol ip prio 16 u32 match ip dst ' + otherSL[2])
            tcout.write(' flowid 1:' + str(handle) + '\n')

            tcout.write('sudo tc qdisc add dev ' + config['network interface name'])
            tcout.write(' parent 1:' + str(handle))

            if sl[0] == otherSL[0]:
                # same BG
                latency = int(int(config['intra-bg roundtrip latency in ms']) / 2)
            else:
                # inter-BG link
                latency = int(int(config['inter-bg roundtrip latency in ms']) / 2)
            tcout.write(' netem delay ' + str(latency) + 'ms\n')

def applyArtificialNetworkLimit(config, SLlist):
    for i in range(0, len(SLlist)):
        full_deploy_path = os.path.join(config['deploy dir'], config['deploy folder prefix'] + str(i))
        subprocess.run(['bash', 'remoteStart.sh', config['username'], SLlist[i][2], full_deploy_path, tc_script])

def clearArtificialNetworkLimit(config, machine_list):
    subprocess.run(['bash', 'clear-tc.sh', machine_list, config['network interface name']])

def main():
    parser = argparse.ArgumentParser(description = '')
    parser.add_argument('-c', '--config-file', type=str, dest='config', required=True)
    parser.add_argument('--kill-only', action='store_true', dest='killonly', required=False)

    args = parser.parse_args()

    with open(args.config) as config:
        lines = config.readlines()

    config_parameters = {}
    runs = []

    run_description = False
    for line in lines:
        if not line:
            continue
        if line.startswith('#'):
            continue
        
        line = line.strip().strip('\n')
        if line == 'BEGIN_RUN_DESCRIPTION':
            run_description = True
            continue

        if not run_description:
            segs = line.split(':')
            kstr = segs[0].strip()
            vstr = segs[1].strip()

            config_parameters[kstr] = vstr
        else:
            segs = line.split(',')
            for i in range(0, len(segs)):
                segs[i] = segs[i].strip()

            run_dict = {'run_rate': int(segs[0]), 'run_tag': segs[1], 'run_time_length': int(segs[2])}
            runs.append(run_dict)


    BGinfo, SLlist = parse_temp(config_parameters)
    print(BGinfo)
    print(SLlist)

    machines = set()
    for sl in SLlist:
        machines.add(sl[2])

    print(machines)

    machine_file = 'machines.conf'

    with open(machine_file, 'w') as machine_config:
        for machine in machines:
            machine_config.write(machine + '\n')

    if(args.killonly):
        dirs = duplicate(config_parameters, BGinfo, SLlist, runs[0])
        stop(config_parameters, SLlist)
        delete(config_parameters, dirs, SLlist)
    else:
        for run in runs:
            dirs = duplicate(config_parameters, BGinfo, SLlist, run)
            deploy(config_parameters, dirs, SLlist, machine_file)
            print("Start servers...")
            applyArtificialNetworkLimit(config_parameters, SLlist)
            start(config_parameters, SLlist)
            time.sleep(run['run_time_length'])
            stop(config_parameters, SLlist)
            print("Servers stopped")
            clearArtificialNetworkLimit(config_parameters, machine_file)
            collect(config_parameters, SLlist, run)
            delete(config_parameters, dirs, SLlist)
            clearLocalDirs(dirs)


if __name__ == "__main__":
    main()

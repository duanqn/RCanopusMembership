import argparse
import os

def sortTime(data_tuple):
    return data_tuple[0]

def sortLatency(data_tuple):
    return data_tuple[2]

def calcAvgLatency(unsorted_tuple_list):
    sum_latency = 0
    for data_tuple in unsorted_tuple_list:
        sum_latency += data_tuple[2]

    return sum_latency / len(unsorted_tuple_list)

def calcPercentileLatency(unsorted_tuple_list, percentile):
    unsorted_tuple_list.sort(key=sortLatency, reverse=False)
    pos = int(len(unsorted_tuple_list) * percentile)
    return unsorted_tuple_list[pos][2]

def calcThroughput(unsorted_tuple_list, time_interval):
    sum_txn = 0
    for data_tuple in unsorted_tuple_list:
        sum_txn += data_tuple[1]

    unsorted_tuple_list.sort(key=sortTime, reverse=False)

    print(str(sum_txn) + " transactions committed in " + str(time_interval) + ' seconds')
    return sum_txn / time_interval

def main():
    parser = argparse.ArgumentParser(description = '')
    parser.add_argument('-p', '--log-parent-path', type=str, dest='log_parent', required=True, help='Path to the parent folder of the log folder')
    parser.add_argument('-f', '--cut-front', type=int, dest='cut_front', required=False, default=15, help='Seconds to cut off at the beginning of the experiment')
    parser.add_argument('-e', '--cut-end', type=int, dest='cut_end', required=False, default=10, help='Seconds to cut off at the end of the experiment')
    parser.add_argument('-o', '--output', type=str, dest='output', required=True, help='File to store the analyze result')

    args = parser.parse_args()

    with open(args.output, 'w') as fout:
        fout.write('# Tag | Avg latency (ms) | 5-th percentile latency (ms) | 25-th percentile latency (ms) | 50-th percentile latency (ms) | 75-th percentile latency (ms) | 95-th percentile latency (ms) | Throughput (tps)\n')

    parent_folder = os.path.abspath(args.log_parent)
    folder_list = os.listdir(parent_folder)
    folder_list.sort()
    for folder_name in folder_list:
        folder = os.path.join(parent_folder, folder_name)
        if not os.path.isdir(folder):
            continue
        if folder_name.startswith('.'):
            continue
        print('Processing folder ' + folder_name)
        tag = os.path.basename(folder)
        files = os.listdir(folder)

        all_committed_result = []
        for filename in files:
            filepath = os.path.join(folder, filename)
            if not filename.endswith('.log'):
                continue

            with open(filepath, 'r') as fin:
                lines = fin.readlines()

            for line in lines:
                line = line.strip().strip('\n')
                if not line:
                    continue
                if not line.startswith('!'):
                    continue
                line = line[1:] # get rid of '!'
                
                segs = line.split('|')

                for i in range(0, len(segs)):
                    segs[i] = segs[i].strip()

                commit_time = float(segs[0])
                transactions = int(segs[2])
                latency = float(segs[3])
                all_committed_result.append((commit_time, transactions, latency))

        all_committed_result.sort(key=sortTime, reverse=False)
        # print(all_committed_result)

        ERRFLAG = False
        try:
            qualified_committed_result = []
            earliest_time = all_committed_result[0][0]
            timestamp_threshold_front = earliest_time + args.cut_front
            latest_time = all_committed_result[-1][0]
            timestamp_threshold_end = latest_time - args.cut_end

            for result_tuple in all_committed_result:
                tuple_time = sortTime(result_tuple)
                if tuple_time >= timestamp_threshold_front and tuple_time <= timestamp_threshold_end:
                    qualified_committed_result.append(result_tuple)
        except Exception as e:
            ERRFLAG = True

        if not ERRFLAG:
            with open(args.output, 'a') as fout:
                try:
                    fout.write(' | '.join([tag, str(calcAvgLatency(qualified_committed_result)), str(calcPercentileLatency(qualified_committed_result, 0.05)), str(calcPercentileLatency(qualified_committed_result, 0.25)), str(calcPercentileLatency(qualified_committed_result, 0.5)), str(calcPercentileLatency(qualified_committed_result, 0.75)), str(calcPercentileLatency(qualified_committed_result, 0.95)), str(calcThroughput(qualified_committed_result, time_interval=timestamp_threshold_end - timestamp_threshold_front))]))
                    fout.write('\n')
                except Exception as e:
                    ERRFLAG = True
        
        if not ERRFLAG:
            try:
                print('Avg latency: ' + str(calcAvgLatency(qualified_committed_result)) + ' ms')
                print('5-th percentile latency: ' + str(calcPercentileLatency(qualified_committed_result, 0.05)) + ' ms')
                print('25-th percentile latency: ' + str(calcPercentileLatency(qualified_committed_result, 0.25)) + ' ms')
                print('50-th percentile latency: ' + str(calcPercentileLatency(qualified_committed_result, 0.5)) + ' ms')
                print('75-th percentile latency: ' + str(calcPercentileLatency(qualified_committed_result, 0.75)) + ' ms')
                print('95-th percentile latency: ' + str(calcPercentileLatency(qualified_committed_result, 0.95)) + ' ms')
                print('Average throughput: ' + str(calcThroughput(qualified_committed_result, time_interval=timestamp_threshold_end - timestamp_threshold_front)) + ' tps')
            except Exception as e:
                ERRFLAG = True

        if ERRFLAG:
            with open(args.output, 'a') as fout:
                fout.write(' | '.join([tag, "ERROR"]))
                fout.write('\n')
                print('Error occurred during calculation.')

if __name__ == "__main__":
    main()
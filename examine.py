import argparse
import os
import subprocess

def main():
    parser = argparse.ArgumentParser(description = '')
    parser.add_argument('-p', '--log-parent-path', type=str, dest='log_parent', required=True, help='Path to the parent folder of the log folder')

    args = parser.parse_args()

    mark_as_delete = []
    log_folder = os.path.abspath(args.log_parent)
    files = os.listdir(log_folder)
    for file in files:
        fullname = os.path.join(log_folder, file)
        if not os.path.isdir(fullname):
            continue

        sub_files = os.listdir(fullname)
        for sub_file in sub_files:
            if sub_file.startswith('core'):
                mark_as_delete.append(fullname)
                break

    if len(mark_as_delete) > 0:
        print("These folder will be removed:")
        for folder in mark_as_delete:
            print(folder)
        
        yn = input("Confirm? (Y/N) ")
        if yn == 'Y' or yn == 'y':
            for folder in mark_as_delete:
                subprocess.run(['rm', '-r', folder])
        else:
            print('Aborted')
    else:
        print("No crashed runs")

if __name__ == "__main__":
    main()
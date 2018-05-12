import os
import sys
import tarfile

arg = sys.argv[1]

def tardir(path, tar_name):
    with tarfile.open(tar_name, "w:gz") as tar_handle:
        for root, dirs, files in os.walk(path):
            for file in files:
                tar_handle.add(os.path.join(root, file))

tardir(arg, 'rootbase.tar.gz')

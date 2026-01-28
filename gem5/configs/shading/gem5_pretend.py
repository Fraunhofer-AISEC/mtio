#!/usr/bin/env python3

import os
import sys
import subprocess

# get passed arg 
cmd = []
for i in range(1, len(sys.argv)):
    cmd.append(sys.argv[i])

# call command in subprocess
os.system(" ".join(cmd))

# run command and print output
process = subprocess.Popen(cmd, stdout=subprocess.PIPE)
# wait fo command to finish
process.wait()
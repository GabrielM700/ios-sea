#!/usr/bin/env python3
"""Write the demo launchd jobs as plain plists into a LaunchDaemons directory.

  minroot-jobs.py DIR [--hid-stub]   (DIR = <root>/System/Library/LaunchDaemons)

com.sea.late: waits 60 s and prints ps -A again (after the daemons settled). --hid-stub adds com.sea.hidstub:
an experiment that only holds the mach name com.apple.iohideventsystem (logd keeps asking for it; stops the log spam).

launchd bootstraps that directory even when the launchd job cache is not used, so the screen shows
ps / df / mount output either way. One-shot (RunAtLoad only): the screen stops after the output.
(Add 'StartInterval': 30 to the dict to repeat them while debugging.)
"""
import os, plistlib, sys

JOBS = [
    ('com.sea.ps',    ['/bin/ps', '-A']),
    ('com.sea.df',    ['/bin/df']),
    ('com.sea.mount', ['/sbin/mount']),
    ('com.sea.late',  ['/bin/sh', '-c', '/bin/sleep 60; echo ===== ps after 60 s =====; /bin/ps -A']),
]

d = sys.argv[1]
stub = '--hid-stub' in sys.argv[2:]
os.makedirs(d, exist_ok=True)
for label, argv in JOBS:
    p = os.path.join(d, label + '.plist')
    with open(p, 'wb') as f:
        plistlib.dump({'Label': label, 'ProgramArguments': argv, 'RunAtLoad': True,
                       'StandardOutPath': '/dev/console', 'StandardErrorPath': '/dev/console'}, f)
    os.chmod(p, 0o644)
    os.chown(p, 0, 0)	# launchd refuses plists not owned by root
    print('wrote', p)

if stub:
    p = os.path.join(d, 'com.sea.hidstub.plist')
    with open(p, 'wb') as f:
        plistlib.dump({'Label': 'com.sea.hidstub', 'ProgramArguments': ['/bin/sleep', '100000000'],
                       'MachServices': {'com.apple.iohideventsystem': True}}, f)
    os.chmod(p, 0o644)
    os.chown(p, 0, 0)
    print('wrote', p)

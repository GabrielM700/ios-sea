#!/usr/bin/env python3
"""Write the demo launchd jobs as plain plists into a LaunchDaemons directory.

  minroot-jobs.py DIR        (DIR = <root>/System/Library/LaunchDaemons)

launchd bootstraps that directory even when the launchd job cache is not used, so the screen shows
ps / df / mount output either way. Each job runs at load and again every 30 s, so the LAST screen
always has fresh output (the console does not scroll back).
"""
import os, plistlib, sys

JOBS = [
    ('com.sea.ps',    ['/bin/ps', '-A']),
    ('com.sea.df',    ['/bin/df']),
    ('com.sea.mount', ['/sbin/mount']),
]

d = sys.argv[1]
os.makedirs(d, exist_ok=True)
for label, argv in JOBS:
    p = os.path.join(d, label + '.plist')
    with open(p, 'wb') as f:
        plistlib.dump({'Label': label, 'ProgramArguments': argv, 'RunAtLoad': True, 'StartInterval': 30,
                       'StandardOutPath': '/dev/console', 'StandardErrorPath': '/dev/console'}, f)
    os.chmod(p, 0o644)
    os.chown(p, 0, 0)	# launchd refuses plists not owned by root
    print('wrote', p)

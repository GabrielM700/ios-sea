#!/usr/bin/env python3
"""Make the launchd job cache for the minimal RAM root.

  minroot-launchd.py STOCK_LAUNCHD.plist OUT.plist [--keep LABEL ...]

STOCK is /System/Library/xpc/launchd.plist of the iOS root (binary plist, read by
launchd when the kernel is booted with launchd_unsecure_cache=1). Its job table
("LaunchDaemons") is replaced by:

  * com.sea.hello / ps / df / mount: one-shot RunAtLoad jobs that run stock iOS tools
    with stdout/stderr on /dev/console, so the screen shows that launchd, dyld and the
    shared cache work;
  * every stock job named with --keep (e.g. com.apple.logd), with its output on the
    console and _PanicOnCrash renamed so a crash does not panic the kernel.

Everything else in the file (global keys) is left as shipped.
"""
import plistlib, sys

DEMO = [
    ('com.sea.ps',    ['/bin/ps', '-A']),
    ('com.sea.df',    ['/bin/df']),
    ('com.sea.mount', ['/sbin/mount']),
]


def main():
    args = sys.argv[1:]
    keep = set()
    outdir = None
    as_root = '--as-root' in args	# run the kept stock jobs as root (drop UserName/GroupName)
    if as_root:
        args.remove('--as-root')
    if '--dir' in args:	# also write the kept jobs as plain plists there (launchd read them even when it rejected the cache)
        i = args.index('--dir')
        outdir = args[i + 1]
        del args[i:i + 2]
    if '--keep' in args:
        i = args.index('--keep')
        keep = set(args[i + 1:])
        args = args[:i]
    src, dst = args
    with open(src, 'rb') as f:
        plist = plistlib.load(f)
    old = plist.get('LaunchDaemons', {})
    jobs = {}
    for key, job in old.items():
        if job.get('Label') in keep:
            if '_PanicOnCrash' in job:
                job['_PanicOnCrasX'] = job.pop('_PanicOnCrash')
            job['StandardOutPath'] = '/dev/console'
            job['StandardErrorPath'] = '/dev/console'
            if as_root:
                for k in ('UserName', 'GroupName', 'Groups'):
                    job.pop(k, None)
            jobs[key] = job
    missing = keep - {j.get('Label') for j in jobs.values()}
    if missing:
        sys.exit('stock jobs not found: ' + ', '.join(sorted(missing)))
    for label, argv in DEMO:
        jobs['/System/Library/LaunchDaemons/%s.plist' % label] = {
            'Label': label,
            'ProgramArguments': argv,
            'RunAtLoad': True,
            'StandardOutPath': '/dev/console',
            'StandardErrorPath': '/dev/console',
        }
    if outdir:
        import os
        os.makedirs(outdir, exist_ok=True)
        for key, job in jobs.items():
            if job['Label'].startswith('com.sea.'):
                continue
            path = os.path.join(outdir, job['Label'] + '.plist')
            with open(path, 'wb') as f:
                plistlib.dump(job, f)
            os.chmod(path, 0o644)
            os.chown(path, 0, 0)
    plist['LaunchDaemons'] = jobs
    with open(dst, 'wb') as f:
        plistlib.dump(plist, f, fmt=plistlib.FMT_BINARY)
    print('%d jobs (%d stock kept, %d demo)' % (len(jobs), len(jobs) - len(DEMO), len(DEMO)))


main()

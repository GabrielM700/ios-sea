#!/usr/bin/env python3
"""List stock launchd jobs and their programs.

  minroot-programs.py STOCK_LAUNCHD.plist --list [PATTERN]     label, program (all jobs, or those containing PATTERN)
  minroot-programs.py STOCK_LAUNCHD.plist LABEL...             'label<TAB>program' for the labels that exist;
                                                               labels that do not exist are reported on stderr.
"""
import plistlib, sys

args = sys.argv[1:]
with open(args[0], 'rb') as f:
    jobs = plistlib.load(f).get('LaunchDaemons', {})


def prog(job):
    return job.get('Program') or (job.get('ProgramArguments') or [''])[0]


by_label = {j.get('Label'): j for j in jobs.values()}
if len(args) > 1 and args[1] == '--list':
    pat = args[2] if len(args) > 2 else ''
    for label in sorted(by_label, key=str):
        if pat in str(label) or pat in prog(by_label[label]):
            print('%s\t%s' % (label, prog(by_label[label])))
else:
    for label in args[1:]:
        if label in by_label:
            print('%s\t%s' % (label, prog(by_label[label])))
        else:
            print('not in the stock cache: ' + label, file=sys.stderr)

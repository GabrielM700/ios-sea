#!/bin/bash
# Host tests (x86-64 Linux, clang): ramdisk scan/realign (lt) and whole loader_boot against a payload (lb).
#   shim-sea/test/run.sh PAYLOAD.bin
set -euo pipefail
cd "$(dirname "$0")"
PAYLOAD=${1:?payload from tools/mkpayload.py}
python3 - <<'PY'
import re
s = open('../loader.c').read()
open('loader_part.c', 'w').write(s[:s.index('const char *loader_boot')])
a = s.index('static void wait_s'); b = s.index('}\n', a) + 2
s = s[:a] + 'static void wait_s(unsigned n) { (void)n; }\n' + s[b:]
s = s.replace('__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");', ';')
s = re.sub(r'__asm__ volatile\("msr sctlr_el1[^;]*;[^;]*;', '(void)0;', s)
s = s.replace('((xnu_entry_t)entry_pa)(ba_pa);', 'host_jump(entry_pa, ba_pa);')
open('loader_host.c', 'w').write(s)
PY
clang -O1 -Wno-everything -I .. -I . -o lt lt.c && ./lt
clang -O1 -Wno-everything -I .. -I . -o lb lb.c && ./lb "$PAYLOAD" | tail -1
rm -f loader_part.c loader_host.c lt lb

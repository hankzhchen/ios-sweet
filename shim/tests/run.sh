#!/bin/bash
# Host tests for the shim's payload search (loader.c) and page tables (mmu.c), yupik layout.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
python3 -I -c '
import re, sys
t = open(sys.argv[1]).read()
t = re.sub(r"^#define [rw]sr\(.*$", "", t, flags=re.M)
t = t.replace("extern char _head[], _end[];", "")
t = re.sub(r"__asm__ volatile\(.*?\);$", ";", t, flags=re.M)
open("out/mmu_host.c", "w").write(t)' ../mmu.c
CF="-O1 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -I.. -I. -DBOARD_YUPIK -fsanitize=address,undefined"
cc $CF -o out/loader_test loader_test.c && ./out/loader_test
cc $CF -o out/mmu_test mmu_test.c && ./out/mmu_test | tail -1

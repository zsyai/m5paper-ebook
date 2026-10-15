#!/usr/bin/env python3
"""Summarizes [PERF] lines captured from an m5paper_perf build.

Usage:
  pio device monitor -e m5paper_perf -p <port>   # log2file -> logs/*.log
  python3 tools/perf_report.py logs/device-monitor-<timestamp>.log [...]

Prints scopes aggregated by name (count / total / max ms), plus marks and
values in log order, so boot and file-open baselines can be compared across
refactor phases.
"""

import collections
import re
import sys

_SCOPE_RE = re.compile(r"\[PERF\] (\S+)\s+(\s*)(\S.*?)  (\d+)\.(\d{3}) ms$")
_MARK_RE = re.compile(r"\[PERF\] (\S+)\s+@(\d+\.\d{3}) ms (\S+) (.*)$")
_VALUE_RE = re.compile(r"\[PERF\] (\S+)\s+(\S.*?) = (-?\d+)$")


def main(argv):
  if len(argv) < 2:
    print(__doc__)
    return 2

  totals = collections.OrderedDict()
  events = []
  lines = []
  for path in argv[1:]:
    with open(path, encoding="utf-8", errors="replace") as f:
      lines.extend(f)
  if True:
    for raw in lines:
      line = raw.rstrip("\r\n")
      if "[PERF]" not in line:
        continue
      line = line[line.index("[PERF]"):]
      m = _MARK_RE.match(line)
      if m:
        events.append(f"@{m.group(2):>10} ms  {m.group(3)}  {m.group(4)}")
        continue
      m = _SCOPE_RE.match(line)
      if m:
        ms = int(m.group(4)) + int(m.group(5)) / 1000.0
        key = (m.group(1), m.group(3))
        count, total, peak = totals.get(key, (0, 0.0, 0.0))
        totals[key] = (count + 1, total + ms, max(peak, ms))
        continue
      m = _VALUE_RE.match(line)
      if m:
        events.append(f"{'':>14}  {m.group(2)} = {m.group(3)}")

  print("== Marks / values (log order) ==")
  for e in events:
    print(e)
  print()
  print(f"== Scopes ==\n{'task':<14}{'scope':<34}{'n':>5}"
        f"{'total ms':>12}{'max ms':>10}")
  for (task, name), (count, total, peak) in totals.items():
    print(f"{task:<14}{name:<34}{count:>5}{total:>12.1f}{peak:>10.1f}")
  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv))

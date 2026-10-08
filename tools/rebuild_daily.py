# Rebuilds /rollups/daily.csv rows from the minute logs, the same way the firmware does
# (updateDailyExtremes(): every minute row counts; a temperature only when the probe was OK).
# Usage: python -I rebuild_daily.py <folder with 2026-09.csv, 2026-10.csv, daily.csv> [last_day]
import sys, os, collections
folder = sys.argv[1]
first_day = "2026-09-27"   # the PlatformIO port; earlier rows are the Arduino sketch's (user, 2026-10-08)
last_day = sys.argv[2] if len(sys.argv) > 2 else "9999-99-99"
MAX_RPM = 4000              # above = tach glitch (fan 2 6083/6180 on 2026-09-29; real top ~2400)
HEADER = "date,local_min_c,local_max_c,network_min_c,network_max_c,blended_min_c,blended_max_c,fan1_min_rpm,fan1_max_rpm,fan2_min_rpm,fan2_max_rpm"

days = collections.OrderedDict()
for name in sorted(f for f in os.listdir(folder) if f[:4].isdigit() and f.endswith(".csv")):
    for line in open(os.path.join(folder, name)):
        f = line.strip().split(",")
        if len(f) not in (6, 8) or not f[0][:4].isdigit():
            continue                      # column names, old BOOT lines mixed into the log
        try:
            rpm1, rpm2 = int(f[4]), int(f[5])
        except ValueError:
            continue
        date = f[0][:10]
        if date > last_day or date < first_day:
            continue
        def temp(s):                      # empty = probe failed; 0.0 = failed in the old 6-field rows
            if s == "": return None
            v = float(s)
            return None if (len(f) == 6 and v == 0.0) else v
        d = days.setdefault(date, {"t": [[], [], []], "r1": [], "r2": [], "rows": 0})
        for i in range(3):
            v = temp(f[1 + i])
            if v is not None: d["t"][i].append(v)
        if rpm1 <= MAX_RPM: d["r1"].append(rpm1)
        if rpm2 <= MAX_RPM: d["r2"].append(rpm2)
        d["rows"] += 1

def rng(vals, lo_none, hi_none, fmt):
    return (fmt(min(vals)), fmt(max(vals))) if vals else (lo_none, hi_none)

out = [HEADER]
for date, d in days.items():
    f1 = lambda v: "%.1f" % v
    cols = []
    for i in range(3):
        cols += rng(d["t"][i], "1000000.0", "-1000000.0", f1)   # the firmware's "no samples" markers
    cols += rng(d["r1"], "999999", "-1", str) + rng(d["r2"], "999999", "-1", str)
    out.append(date + "," + ",".join(cols))
    print("%s rows=%4d  %s" % (date, d["rows"], ",".join(cols)), file=sys.stderr)
open(os.path.join(folder, "daily_rebuilt.csv"), "w", newline="\n").write("\n".join(out) + "\n")

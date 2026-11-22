"""Summarize single-dispatch SORT replays using seed-level means."""

import argparse
import csv
from collections import defaultdict
from math import sqrt
from pathlib import Path
from statistics import mean, stdev


def summarize(path):
    with path.open(newline="", encoding="utf-8") as data:
        rows = list(csv.DictReader(data))
    print(f"{path}: {sum(row['complete'] == '1' for row in rows)}/{len(rows)} complete")
    by_group = defaultdict(list)
    for row in rows:
        if row["complete"] == "1":
            by_group[int(row["group"])].append(row)

    def value(row, name):
        return float(row[name])

    def delta(row, field):
        return value(row, field + "_test") - value(row, field + "_base")

    for group, trials in sorted(by_group.items()):
        by_seed = defaultdict(list)
        for trial in trials:
            by_seed[trial["seed"]].append(trial)

        def report(label, measure):
            scores = [mean(measure(row) for row in seed_trials) for seed_trials in by_seed.values()]
            se = stdev(scores) / sqrt(len(scores)) if len(scores) > 1 else float("nan")
            print(f"  {label}: {mean(scores):+.4f} (SE {se:.4f})")

        sooner = sum(delta(row, "urgent_pickup") < -1e-4 for row in trials)
        later = sum(delta(row, "urgent_pickup") > 1e-4 for row in trials)
        direct = sum(row["next_urgent_uses_inbound"] == "1" for row in trials)
        group_name = f"other inbound {'yes' if group & 2 else 'no'}, de {'>25' if group & 1 else '<=25'}"
        print(f"group {group}: {group_name}, n={len(trials)}, seeds={len(by_seed)}")
        print(f"  next pickup sooner/same/later = {sooner}/{len(trials)-sooner-later}/{later}")
        print(f"  urgent carried by the vehicle from the promoted delivery: {direct}")
        report("new urgent arrival after intervention (min)", lambda r: value(r, "urgent_arrival") - value(r, "time"))
        report("promoted SORT release after intervention (min)", lambda r: value(r, "sort_free_test") - value(r, "time"))
        report("paired SORT release difference (min)", lambda r: delta(r, "sort_free"))
        report("paired next urgent pickup difference (min)", lambda r: delta(r, "urgent_pickup"))
        report("paired displaced pickup difference (min)", lambda r: delta(r, "original_pickup"))
        report("paired 10-min cohort wait difference per request (min)",
               lambda r: value(r, "cohort_pickup_delta") / max(1, value(r, "cohort_size")))
        report("paired full-scenario cost difference", lambda r: delta(r, "cost"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("replay_csv", nargs="+", type=Path)
    for file in parser.parse_args().replay_csv:
        summarize(file)

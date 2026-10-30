#!/usr/bin/env python3
"""Summarize per-issue research records (research/issues/issue-*.yaml).

Computes the metrics preregistered in research/protocol.md:
  - issues closed by category
  - AI fix acceptance rate (overall and per category, Clopper-Pearson 95% CI)
  - which verification layer caught each incorrect attempt (RQ1)
  - number and size of result-affecting changes (RQ4)
  - human time and calendar days per issue (RQ3)

Records are validated strictly: an unknown value is an error, not a guess.

Usage:
  python3 research/scripts/summarize.py [--issues DIR] [--json]
"""

import argparse
import datetime
import glob
import json
import math
import os
import sys
from collections import Counter, defaultdict

import yaml

CATEGORIES = ["silent-wrong-result", "crash", "build", "usability", "enhancement"]
REPRODUCED = ["yes", "no", "already-fixed"]
OUTCOMES = ["accepted", "caught-by-test", "superficially-right", "wrong"]
LAYERS = ["smoke", "unit", "golden", "invariant", "reviewer", "maintainer"]
CAUGHT_BY = LAYERS + ["post-merge"]
LAYER_RESULTS = ["pass", "fail", "not-run", "not-applicable"]
FOUND_BY = ["ai", "human", "joint"]
TIME_BUCKETS = ["<30m", "30m-2h", "2h-4h", "4h-8h", "1d-2d", ">2d"]
# Midpoints in hours, used only for rough totals; buckets are reported as-is.
TIME_MIDPOINT_H = {"<30m": 0.25, "30m-2h": 1.25, "2h-4h": 3, "4h-8h": 6,
                   "1d-2d": 12, ">2d": 24}
RISKS = ["none", "low", "possible", "likely"]
BACKFILL_DAYS = 7


class RecordError(Exception):
  pass


def check_enum(path, field, value, allowed, nullable=True):
  if value is None and nullable:
    return
  if value not in allowed:
    raise RecordError("%s: %s=%r not in %s" % (path, field, value, allowed))


def as_date(path, field, value):
  if value is None:
    return None
  if isinstance(value, datetime.date):
    return value
  try:
    return datetime.date.fromisoformat(str(value))
  except ValueError:
    raise RecordError("%s: %s=%r is not an ISO date" % (path, field, value))


def load_record(path):
  with open(path) as f:
    rec = yaml.safe_load(f)
  if not isinstance(rec, dict) or not isinstance(rec.get("issue"), int):
    raise RecordError("%s: missing integer 'issue'" % path)
  check_enum(path, "category", rec.get("category"), CATEGORIES, nullable=False)
  for field in ("date_started", "date_closed", "record_filled"):
    rec[field] = as_date(path, field, rec.get(field))
  repro = rec.get("reproduction") or {}
  check_enum(path, "reproduction.status", repro.get("status"), REPRODUCED)
  for i, att in enumerate(rec.get("ai_attempts") or []):
    where = "ai_attempts[%d]" % i
    check_enum(path, where + ".outcome", att.get("outcome"), OUTCOMES)
    check_enum(path, where + ".caught_by", att.get("caught_by"), CAUGHT_BY)
    for layer in LAYERS:
      check_enum(path, "%s.layers.%s" % (where, layer),
                 (att.get("layers") or {}).get(layer, "not-run"), LAYER_RESULTS,
                 nullable=False)
    if att.get("outcome") == "accepted" and att.get("caught_by"):
      raise RecordError("%s: %s is accepted but has caught_by" % (path, where))
    if att.get("outcome") == "caught-by-test" and att.get("caught_by") not in (
        "smoke", "unit", "golden", "invariant"):
      raise RecordError("%s: %s is caught-by-test but caught_by is not an "
                        "automated layer" % (path, where))
  check_enum(path, "root_cause.found_by",
             (rec.get("root_cause") or {}).get("found_by"), FOUND_BY)
  check_enum(path, "human_time", rec.get("human_time"), TIME_BUCKETS)
  check_enum(path, "scientific_impact.published_results_risk",
             (rec.get("scientific_impact") or {}).get("published_results_risk"),
             RISKS)
  return rec


def binom_cdf(k, n, p):
  return sum(math.comb(n, i) * p ** i * (1 - p) ** (n - i) for i in range(k + 1))


def clopper_pearson(k, n, alpha=0.05):
  """Exact two-sided CI for a binomial proportion, by bisection."""
  if n == 0:
    return (None, None)

  def solve(f):
    lo, hi = 0.0, 1.0
    for _ in range(60):
      mid = (lo + hi) / 2
      if f(mid):
        lo = mid
      else:
        hi = mid
    return (lo + hi) / 2

  lower = 0.0 if k == 0 else solve(lambda p: 1 - binom_cdf(k - 1, n, p) < alpha / 2)
  upper = 1.0 if k == n else solve(lambda p: binom_cdf(k, n, p) > alpha / 2)
  return (lower, upper)


def rate(k, n):
  lo, hi = clopper_pearson(k, n)
  return {"k": k, "n": n, "rate": (k / n) if n else None, "ci95": [lo, hi]}


def in_effort_denominator(rec):
  """Protocol exclusions: already-fixed / cannot-reproduce leave RQ2-3."""
  return (rec.get("reproduction") or {}).get("status") not in ("no", "already-fixed")


def summarize(records):
  out = {"n_records": len(records)}
  closed = [r for r in records if r["date_closed"]]
  out["closed_by_category"] = dict(Counter(r["category"] for r in closed))
  out["open_by_category"] = dict(
      Counter(r["category"] for r in records if not r["date_closed"]))
  out["reproduction"] = dict(Counter(
      (r.get("reproduction") or {}).get("status") or "unknown" for r in records))

  # RQ2: acceptance rate.
  attempts = [(r, a) for r in records if in_effort_denominator(r)
              for a in (r.get("ai_attempts") or []) if a.get("outcome")]
  out["acceptance"] = {"overall": rate(
      sum(a["outcome"] == "accepted" for _, a in attempts), len(attempts))}
  for cat in CATEGORIES:
    cat_att = [a for r, a in attempts if r["category"] == cat]
    if cat_att:
      out["acceptance"][cat] = rate(
          sum(a["outcome"] == "accepted" for a in cat_att), len(cat_att))
  out["outcomes"] = dict(Counter(a["outcome"] for _, a in attempts))

  # RQ1: which layer caught each incorrect attempt.
  incorrect = [(r, a) for r, a in attempts if a["outcome"] != "accepted"]
  out["caught_by"] = dict(Counter(a.get("caught_by") or "uncaught" for _, a in incorrect))
  passed_smoke_caught_new = [
      "#%d/attempt-%s" % (r["issue"], a.get("id"))
      for r, a in incorrect
      if (a.get("layers") or {}).get("smoke") == "pass"
      and "fail" in ((a.get("layers") or {}).get("golden"),
                     (a.get("layers") or {}).get("invariant"))]
  out["rq1_passed_smoke_caught_by_golden_or_invariant"] = {
      "count": len(passed_smoke_caught_new), "attempts": passed_smoke_caught_new}
  out["rq1_incorrect_missed_by_all_automated_layers"] = sum(
      a.get("caught_by") in ("reviewer", "maintainer", "post-merge", None)
      for _, a in incorrect)

  # RQ3: root cause and effort.
  effort = [r for r in records if in_effort_denominator(r)]
  rc = defaultdict(Counter)
  time_by_cat = defaultdict(Counter)
  days = defaultdict(list)
  for r in effort:
    rc[r["category"]][(r.get("root_cause") or {}).get("found_by") or "unknown"] += 1
    time_by_cat[r["category"]][r.get("human_time") or "unknown"] += 1
    if r["date_started"] and r["date_closed"]:
      days[r["category"]].append((r["date_closed"] - r["date_started"]).days)
  out["root_cause_found_by"] = {k: dict(v) for k, v in rc.items()}
  out["human_time_buckets"] = {k: dict(v) for k, v in time_by_cat.items()}
  out["human_time_rough_hours"] = {
      cat: sum(TIME_MIDPOINT_H[b] * n for b, n in c.items() if b in TIME_MIDPOINT_H)
      for cat, c in time_by_cat.items()}
  out["calendar_days"] = {
      cat: {"n": len(d), "median": sorted(d)[len(d) // 2], "max": max(d)}
      for cat, d in days.items() if d}

  # RQ4: result-affecting changes.
  changed = [r for r in records if (r.get("scientific_impact") or {}).get("outputs_changed")]
  out["result_affecting"] = {
      "issues": len(changed),
      "risk": dict(Counter(
          (r.get("scientific_impact") or {}).get("published_results_risk") or "unknown"
          for r in changed)),
      "changes": [
          dict(issue=r["issue"], **{k: c.get(k) for k in (
              "command", "output", "column", "max_abs_delta", "max_rel_delta",
              "rows_affected", "rows_total")})
          for r in changed
          for c in (r.get("scientific_impact") or {}).get("changes") or []
          if c.get("command")],
  }

  # Protocol integrity: back-filled records.
  out["backfilled_records"] = [
      r["issue"] for r in records
      if r["date_closed"] and r["record_filled"]
      and (r["record_filled"] - r["date_closed"]).days > BACKFILL_DAYS]
  return out


def fmt_rate(d):
  if not d["n"]:
    return "n/a (0 attempts)"
  return "%d/%d = %.0f%% (95%% CI %.0f–%.0f%%)" % (
      d["k"], d["n"], 100 * d["rate"], 100 * d["ci95"][0], 100 * d["ci95"][1])


def print_markdown(s):
  print("# Research summary (%d records)\n" % s["n_records"])
  print("## Issues closed by category")
  for cat in CATEGORIES:
    print("- %s: %d closed, %d open" % (cat, s["closed_by_category"].get(cat, 0),
                                        s["open_by_category"].get(cat, 0)))
  print("- reproduction status: %s\n" % s["reproduction"])

  print("## RQ2 — AI fix acceptance")
  print("- overall: " + fmt_rate(s["acceptance"]["overall"]))
  for cat in CATEGORIES:
    if cat in s["acceptance"]:
      print("- %s: %s" % (cat, fmt_rate(s["acceptance"][cat])))
  print("- outcomes: %s\n" % s["outcomes"])

  print("## RQ1 — Layer that caught each incorrect attempt")
  for layer in CAUGHT_BY + ["uncaught"]:
    if layer in s["caught_by"]:
      print("- %s: %d" % (layer, s["caught_by"][layer]))
  key = s["rq1_passed_smoke_caught_by_golden_or_invariant"]
  print("- **passed smoke, failed golden/invariant: %d** %s" % (key["count"], key["attempts"]))
  print("- incorrect, missed by all automated layers: %d\n"
        % s["rq1_incorrect_missed_by_all_automated_layers"])

  print("## RQ3 — Root cause and effort")
  for cat in CATEGORIES:
    if cat in s["root_cause_found_by"]:
      print("- %s: root cause %s; time %s; ~%.1f h; days %s" % (
          cat, s["root_cause_found_by"][cat], s["human_time_buckets"][cat],
          s["human_time_rough_hours"][cat], s["calendar_days"].get(cat, "n/a")))
  print()

  ra = s["result_affecting"]
  print("## RQ4 — Result-affecting changes")
  print("- issues with changed outputs: %d; published-results risk: %s" % (ra["issues"], ra["risk"]))
  if ra["changes"]:
    print("\n| issue | command | output | column | max abs Δ | max rel Δ | rows affected/total |")
    print("|---|---|---|---|---|---|---|")
    for c in ra["changes"]:
      print("| #%s | %s | %s | %s | %s | %s | %s/%s |" % (
          c["issue"], c["command"], c["output"], c["column"], c["max_abs_delta"],
          c["max_rel_delta"], c["rows_affected"], c["rows_total"]))
  print()

  if s["backfilled_records"]:
    print("## ⚠ Back-filled records (> %d days after close)" % BACKFILL_DAYS)
    print("- " + ", ".join("#%d" % i for i in s["backfilled_records"]))


def main():
  here = os.path.dirname(os.path.abspath(__file__))
  parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
  parser.add_argument("--issues", default=os.path.join(here, "..", "issues"),
                      help="directory containing issue-*.yaml")
  parser.add_argument("--json", action="store_true", help="emit JSON instead of Markdown")
  args = parser.parse_args()

  paths = sorted(glob.glob(os.path.join(args.issues, "issue-*.yaml")))
  try:
    records = [load_record(p) for p in paths]
  except RecordError as e:
    sys.exit("error: %s" % e)
  dupes = [n for n, c in Counter(r["issue"] for r in records).items() if c > 1]
  if dupes:
    sys.exit("error: duplicate records for issues %s" % dupes)

  summary = summarize(records)
  if args.json:
    json.dump(summary, sys.stdout, indent=2, default=str)
    print()
  else:
    print_markdown(summary)


if __name__ == "__main__":
  main()

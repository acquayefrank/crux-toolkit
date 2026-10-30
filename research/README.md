# Research tracking

This folder tracks the study *A scientific approach to developing scientific
software in the age of AI*. It records the work in `specs/roadmap.md` as
data for that study.

| Path | What | When to touch it |
|---|---|---|
| `protocol.md` | Preregistration: RQs, metrics, hypotheses, success/failure | Never, except for logged deviations |
| `threats-to-validity.md` | Threats and how each is mitigated | When a threat appears or changes status |
| `decisions.md` | Dated, append-only decision log | Every non-trivial choice, version change or deviation |
| `issues/TEMPLATE.yaml` → `issues/issue-<N>.yaml` | Per-issue record | When work on an issue starts, after each AI attempt, and at close |
| `weekly/TEMPLATE.md` → `weekly/YYYY-MM-DD.md` | 10-minute weekly entry | Once a week |
| `evidence/` | Golden outputs, diffs, redacted transcripts | Per `evidence/README.md` |
| `scripts/summarize.py` | Computes the protocol metrics | Any time; the version run for the paper is tagged |

```sh
python3 -m pip install -r research/scripts/requirements.txt
python3 research/scripts/summarize.py          # Markdown
python3 research/scripts/summarize.py --json   # for analysis notebooks
```

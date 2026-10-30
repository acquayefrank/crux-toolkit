# Threats to validity

Recorded from the start (2026-10-30). Add to this file as new threats show
up. Never delete an entry; mark it `mitigated` or `materialized`, with a
date.

## Internal validity

| # | Threat | Mitigation | Status |
|---|---|---|---|
| I1 | **Investigator is also the developer and the observer.** Classifying attempts as "wrong" or "superficially right" is a judgement call, and it could be biased toward the story the paper tells. | Outcome definitions are fixed in `issues/TEMPLATE.yaml` before data collection. Ambiguous cases are logged in `decisions.md`. A maintainer re-codes a random 20% sample, and we report agreement (Cohen's κ). | open |
| I2 | **No control group.** There is no human-only or AI-only arm, so we can't make causal claims about AI vs. no-AI productivity. | Claims are limited to describing the layered process. Where possible, compare with historic issue-closure times in the repo (exploratory only). | open |
| I3 | **The AI helps build the checks that judge the AI.** Golden outputs and invariant checks may be written with AI help (a circularity risk). | Golden values come from an independent reference (standalone tools, hand calculation). Each check records whether it was AI-assisted (`decisions.md`). The invariant formulas are written down in the specs before use. | open |
| I4 | **Learning effects.** Both the investigator and the prompting style improve over time, so later issues may look easier. | Record `date_started` and phase order. Report trends over time. Log any process changes in the weekly entries. | open |
| I5 | **Recording burden and back-filling.** Records filled in late lose detail and pick up hindsight bias. | Each record carries a `record_filled` date. Records filled more than 7 days after the event are flagged in the analysis. | open |
| I6 | **Which attempts get counted.** Attempts dropped before any test runs aren't counted, which can make AI quality look better than it is. | The definition is fixed in `protocol.md`. Transcripts are archived, so readers can count discarded proposals themselves. | open |

## External validity

| # | Threat | Mitigation | Status |
|---|---|---|---|
| E1 | **One codebase.** Crux is C++, mass-spectrometry specific and has a long history, so results may not carry over. | Describe the codebase in detail (size, age, test coverage at baseline). Present results as a case study. | open |
| E2 | **Selection bias in issues.** The open-issue backlog is skewed toward hard or unglamorous problems, because the easy ones were closed long ago. | Report the category mix compared with closed issues in the repo's history. | open |
| E3 | **One investigator.** The results depend on one person's skill and domain knowledge. | Report the investigator's background. Invite replication through the replication package. | open |
| E4 | **Maintainer reviewers are not typical.** The original authors know the code very well. | Report reviewer roles (not names) and their familiarity with the code. | open |

## Construct validity

| # | Threat | Mitigation | Status |
|---|---|---|---|
| C1 | **"Superficially right" is fuzzy.** | Operational definition: passes every automated layer that ran, but is wrong under an independent reference or on human review. | open |
| C2 | **Time buckets are coarse self-reports.** | Fixed buckets, recorded on the day. Calendar duration from git and GitHub timestamps is reported alongside. | open |
| C3 | **The tolerance policy defines "changed results."** A looser tolerance hides changes. | `TOLERANCES.md` is versioned. Report deltas as raw numbers, not just pass/fail. | open |

## Reliability and reproducibility

| # | Threat | Mitigation | Status |
|---|---|---|---|
| R1 | **AI model and tool version drift.** Models and Claude Code update during the study, so behaviour shifts. | Record the model ID and tool version on every attempt. Log version changes in `decisions.md`. Report results per model version. | open |
| R2 | **AI output is not deterministic.** The same prompt may produce a different fix. | Archive redacted transcripts. Don't claim that a given fix can be reproduced from its prompt. | open |
| R3 | **Upstream dependencies change** (Comet, Percolator, pwiz pins). | Pins are fixed per `specs/tech-stack.md`. Any bump is logged as a decision and its golden-output diffs are archived. | open |
| R4 | **GitHub issue text gets edited or deleted.** | Snapshot each issue's text into `evidence/issue-<N>/` when work starts. | open |

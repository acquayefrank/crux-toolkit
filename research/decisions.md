# Decision log

Newest entries go at the bottom. Never edit or delete a past entry. If a
decision is reversed, add a new entry that references the old one.
Protocol deviations, AI model or tool version changes, and maintainer
consent are always logged here.

Entry format:

```
## D-NNN — YYYY-MM-DD — <short title>
- **Decision:** what was chosen.
- **Alternatives considered:** what else, briefly.
- **Why:** reasoning and evidence.
- **Affects:** RQs / files / phases touched.
- **AI involvement:** none | suggested | decided-with-human-approval
```

---

## D-001 — 2026-10-30 — Issue backlog is the study population
- **Decision:** Study the 95 issues open on 2026-10-30, in the order given
  by `specs/roadmap.md` (most scientific risk first).
- **Alternatives considered:** a random sample of issues; only bug-labelled
  issues; a TODO.md (none existed).
- **Why:** Closing every open issue is the real engineering goal. Ordering
  by scientific risk matches `specs/mission.md`.
- **Affects:** all RQs; `protocol.md`.
- **AI involvement:** suggested (the roadmap was drafted with AI and
  approved by the investigator).

## D-002 — 2026-10-30 — All four RQs confirmatory
- **Decision:** Preregister RQ1–RQ4 as confirmatory, with descriptive
  statistics only.
- **Alternatives considered:** RQ1 and RQ2 confirmatory with the rest
  exploratory.
- **Why:** The investigator wants all four reported. The small N rules out
  inferential claims, so having more confirmatory RQs costs little.
- **Affects:** `protocol.md`.
- **AI involvement:** suggested.

## D-003 — 2026-10-30 — Target venues
- **Decision:** Preprint first (arXiv/bioRxiv). Then aim for the strictest
  plausible venue: an SE research venue (ICSE SEIP / FSE / MSR / EMSE) or
  a respectable top journal. Research-software venues (US-RSE, SE4Science,
  CiSE) and bioinformatics venues (Bioinformatics, JPR, PLoS Comp Bio) are
  candidates for spin-off papers.
- **Alternatives considered:** a single venue chosen now.
- **Why:** Collecting data to the strictest standard (replication package,
  threats to validity, inter-rater agreement) keeps every option open.
- **Affects:** `protocol.md`, `threats-to-validity.md`, `evidence/`.
- **AI involvement:** suggested.

## D-004 — 2026-10-30 — Transcripts redacted, committed in repo
- **Decision:** Commit AI session transcripts to
  `evidence/issue-<N>/transcripts/` after a redaction pass (see
  `evidence/README.md`).
- **Alternatives considered:** hashes in the repo with full transcripts in
  a restricted Zenodo record; full unredacted transcripts.
- **Why:** Public and citable, with low risk of leaking secrets or personal
  data.
- **Affects:** RQ1–RQ3 evidence; `evidence/README.md`.
- **AI involvement:** suggested.

## D-005 — 2026-10-30 — Research scaffold lives on the fork only
- **Decision:** `research/` lives on `acquayefrank/crux-toolkit`, not in an
  upstream PR, until the original maintainers consent to the study and to
  being study participants (review data).
- **Alternatives considered:** an upstream PR now; a separate research repo.
- **Why:** Maintainers are participants, so they must consent first. Git
  timestamps on the fork still preregister the protocol.
- **Affects:** `protocol.md` (consent), review-data fields.
- **AI involvement:** suggested.

## D-006 — 2026-10-30 — Verification layers and outcome coding
- **Decision:** The layers are smoke, unit, golden, invariant, reviewer,
  maintainer and post-merge. `smoke` and `post-merge` were added to the
  list originally requested (unit, golden, invariant, reviewer) because RQ1
  needs a smoke-test baseline and escaped defects need a code. Outcomes are
  mutually exclusive, with the precedence rules in `issues/TEMPLATE.yaml`.
- **Alternatives considered:** recording only the layer that caught the
  fix (that loses the "passed smoke but failed golden" signal).
- **Why:** RQ1's key count needs the result of every layer for every
  attempt.
- **Affects:** RQ1, RQ2; `issues/TEMPLATE.yaml`, `scripts/summarize.py`.
- **AI involvement:** suggested.

## D-007 — 2026-10-30 — Baseline AI tooling
- **Decision:** The study starts with model `claude-opus-5-5` in Claude Code
  2.1.314.
- **Alternatives considered:** —
- **Why:** This is the baseline for tracking version drift (threat R1).
- **Affects:** all RQs.
- **AI involvement:** none.

## D-008 — 2026-10-30 — Pending: maintainer consent
- **Decision:** Before analysing or publishing any maintainer review data,
  get written consent from the original maintainers and record it here as
  a new entry (date and form, with roles, not names).
- **Alternatives considered:** —
- **Why:** Research ethics. Maintainers are participants.
- **Affects:** the `review` fields in issue records.
- **AI involvement:** suggested.

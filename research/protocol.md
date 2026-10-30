# Study protocol (preregistration)

**Title (working):** A scientific approach to developing scientific software
in the age of AI. An AI-assisted, verification-layered study of closing every
open issue in Crux.

**Registered:** 2026-10-30, by the git commit that adds this file, before
roadmap Phase 1 starts. Any later change to this file is a protocol
deviation and must be logged in `decisions.md` with its date and reason.
The original text is kept in git history.

**Investigator:** acquayefrank (developer and observer). The original Crux
maintainers take part as code reviewers. Their consent is recorded in
`decisions.md` before any of their review data is analysed or published.

## Setting and unit of analysis

- **Codebase:** crux-toolkit, starting at upstream `master` @ `0f57aa531`.
- **Population:** the 95 issues open on 2026-10-30, as listed in
  `specs/roadmap.md`. Issues opened later are tracked but analysed
  separately (exploratory).
- **Units:**
  - (a) an *issue*: one `research/issues/issue-<N>.yaml` per issue;
  - (b) an *AI attempt*: one candidate patch, in whole or in part, proposed
    by an AI tool and then run against any test or shown to a human.
    Attempts that are discarded before either happens are not counted.
- **Verification layers:**
  - `smoke`: the existing Cucumber suite;
  - `unit`;
  - `golden`: golden-output comparison under `TOLERANCES.md`;
  - `invariant`: the mission invariants;
  - `reviewer`: the investigator's own review;
  - `maintainer`: upstream review;
  - `post-merge`: found after merge.

  Every attempt is run through the automated layers in this order and the
  result of each is recorded. `caught_by` is the first layer that failed.
- **End of study:** when all 95 issues are closed, or on 2027-04-30,
  whichever comes first. Issues still open at the end are reported as
  censored, not dropped.

## Research questions and metrics (all confirmatory)

| RQ | Question | Primary metric(s) |
|---|---|---|
| **RQ1** Detection layers | Which verification layer catches incorrect AI-proposed fixes? How many fixes that passed the existing smoke tests were caught only by the invariant or golden-output checks? | Distribution of `caught_by` over incorrect attempts. **Key count:** incorrect attempts with `smoke = pass` and `golden` or `invariant` = fail. Share of incorrect attempts caught by no automated layer. |
| **RQ2** AI fix quality | How often are AI-proposed fixes accepted, wrong or superficially right, and does this depend on issue category? | Acceptance rate = accepted / attempts, overall and per category. Attempts per closed issue. |
| **RQ3** Root cause and effort | Who finds the root cause (AI or human), and how much human effort does each category need? | Share of root causes found by AI, human or jointly, per category. Human-time bucket distribution per category. Calendar days from start to close. |
| **RQ4** Scientific impact | How many fixes change scientific outputs, by how much, and could published results be affected? | Count of result-changing fixes. Max abs/rel delta per changed column. Affected rows / total rows. `published_results_risk` distribution. |

**Analysis plan.** The sample is small and comes from one codebase, so all
results are descriptive. We report counts with exact (Clopper–Pearson) 95%
CIs for proportions, and Fisher's exact test for category comparisons in
RQ2 and RQ3. The analysis does not depend on any p-value threshold. Rates
are computed by `research/scripts/summarize.py`; the version run for the
paper is tagged.

**Exclusions.** Issues closed as `already-fixed` or `cannot-reproduce` are
counted in the issue totals but excluded from the RQ2 and RQ3 denominators.
Nothing else is excluded unless logged in `decisions.md` before it is
analysed.

## What counts as success or failure

The study succeeds as a study whatever the results. Null and negative
results get reported. The **hypotheses** below can be falsified:

- **H1 (RQ1):** At least one incorrect AI attempt passes `smoke` but fails
  `golden` or `invariant`. *Falsified* if every incorrect attempt is caught
  by `smoke`, `unit` or a human first.
- **H2 (RQ2):** Acceptance rate is lower for `silent-wrong-result` issues
  than for `build` issues. *Falsified* if it is equal or higher.
- **H3 (RQ3):** Humans find the root cause more often than the AI for
  `silent-wrong-result` issues.
- **H4 (RQ4):** At least one fix changes a scientific output by more than
  the `TOLERANCES.md` threshold in a way that could affect published
  results.

**The process fails** (and the paper must say so) if any of these happen:
- a merged fix later turns out to have changed results incorrectly
  (`post-merge` catch);
- records are back-filled more than 7 days after the event (each record
  notes its fill date);
- the record set and the git history disagree.

## Data and transparency

- **Records:** per-issue YAML (`issues/`), the decision log, and weekly
  entries (`weekly/`).
- **Evidence:** before/after golden outputs, result diffs, and redacted AI
  transcripts (`evidence/`). Conventions are in `evidence/README.md`.
- **AI model and tool versions** are recorded on every attempt. Any version
  change is logged in `decisions.md`.
- Threats to validity are in `threats-to-validity.md` and are kept up to
  date throughout.

# Evidence conventions

Everything a reader needs to check a claim in the paper lives here, or is
linked from here by DOI and hash.

## Layout

```
evidence/
  issue-<N>/
    issue-snapshot.md          # GitHub issue text + comments when work started (threat R4)
    golden-before/             # outputs from master before the fix
    golden-after/              # outputs with the fix
    diff/
      <command>.<output>.diff.tsv   # per-column deltas (see below)
      summary.md               # human-readable: what changed, why it's correct
    transcripts/
      YYYY-MM-DD-<seq>.md      # redacted AI session transcripts, one per session
    SHA256SUMS                 # sha256 of every file in this issue's folder
```

## Golden outputs (before/after)

- Generate both from the same inputs and parameters, on the same platform.
  Record the platform, commit SHA and exact command line at the top of
  `diff/summary.md`.
- Store **only result-affecting outputs** (tsv, pin, pepXML, mzIdentML,
  logs that report numbers). Leave out timing-only logs.
- Files over **5 MB** are not committed. Upload them to the study's Zenodo
  record and add a line to `SHA256SUMS` plus a `LARGE_FILES.md` entry
  (DOI, filename, sha256).

## Diffs for result-affecting changes

Required whenever `scientific_impact.outputs_changed: true`. Each
`*.diff.tsv` has one row per changed column:

```
column	rows_total	rows_affected	max_abs_delta	max_rel_delta	n_nan_before	n_nan_after	note
```

- Deltas are computed under the `TOLERANCES.md` policy that was current
  when the diff was made. Record its git SHA in `summary.md`.
- Non-numeric changes (IDs, sequences, mod strings) give `rows_affected`
  and a sample of up to 10 before/after pairs in `summary.md`.
- The numbers in the issue YAML's `scientific_impact.changes` must match
  these files.

## AI session transcripts (redacted, D-004)

1. Export the session (Claude Code: `/export` or the session JSONL).
2. **Redact** before committing:
   - secrets: tokens, keys, passwords, `gho_…`/`ghp_…`, SSH material, auth
     URLs → `[REDACTED-SECRET]`
   - email addresses → `[REDACTED-EMAIL]`
   - personal names and handles of anyone except the investigator → roles
     (`[maintainer-1]`, `[reviewer]`)
   - absolute home paths → `~/…`
   - private correspondence pasted into the session → `[REDACTED-CORRESPONDENCE]`
3. Keep everything else verbatim, including the AI's mistakes. Don't
   tidy them up.
4. Add a header with the date, model ID, tool version, issue number, the
   attempt IDs the session covers, and `redacted-by: <you>, <date>`.
5. Before committing, run a quick check such as
   `grep -nE 'gh[opsu]_[A-Za-z0-9]{20,}|@[a-z0-9.-]+\.[a-z]{2,}'` on the file.
   It must come back empty or show only intended text.

## Integrity

- Update `SHA256SUMS` whenever files are added or changed. From inside
  `issue-<N>/`, run:
  `find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS`
- Evidence is append-only. To correct something, add a new file and explain
  it in `summary.md`. Never rewrite an old file.

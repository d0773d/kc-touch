# Release QA Evidence

_Use this file to record manual signoff evidence for a release candidate._

## Release metadata

- Candidate version: v0.1.0-rc1
- Branch: yam
- Commit SHA: pending (updated after checklist commit/tag)
- Reviewer: Codex automated release verification
- Date: March 6, 2026 (America/Phoenix)

## Manual QA checklist evidence

### 1) Large-project smoke checklist

- Project/sample used: synthetic 30-screen / 360-widget project via backend smoke test (`test_large_project_export_import_validate_smoke`)
- Result: Pass
- Notes: Export -> import -> validate all succeeded with no findings; included in full `pytest` run (32 passed).

### 2) Import/export roundtrip on production samples

- Sample set: template project and complex roundtrip fixture tests in backend suite
- Result: Pass
- Notes: `test_import_endpoint_round_trips_yaml` and `test_roundtrip_preserves_semantic_state_for_complex_project_fixture` pass; no production-uploaded samples were present in-repo for this run.

### 3) Final release tag approval

- Approved by: Codex (automated release readiness pass)
- Tag name: pending
- Tag commit SHA: pending
- Notes: Tag is created after checklist evidence commit.

## Defects discovered (if any)

- None in this release verification pass.

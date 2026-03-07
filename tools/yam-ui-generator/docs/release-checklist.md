# Release Checklist

_Version: v0.1.0_
_Updated: March 7, 2026_

## Build and test gates

- [x] Frontend lint: `npm run lint`
- [x] Frontend type-check: `npm run typecheck`
- [x] Frontend test suite: `npm test`
- [x] Frontend production build: `npm run build`
- [x] Backend lint: `poetry run ruff check .`
- [x] Backend test suite: `poetry run pytest -q --basetemp .pytest_tmp`
- [x] CI workflow enabled for PR + branch pushes (`main`, `yam`, `codex/**`)

## Contract and compatibility

- [x] `GET /contract` endpoint returns backend contract version and migration support.
- [x] Backend error envelope standardized to `{ error: { code, message, status, details? } }`.
- [x] Legacy import migration path covered by tests and warning emission.
- [x] API contract documented in `docs/api-contract.md`.

## Product workflows

- [x] Live preview supports local + backend findings.
- [x] Snapshot restore flow includes confirmation and optional diff preview.
- [x] Issue accelerators navigate widget/style findings.
- [x] Asset catalog scanning/upload/tagging flows available in UI.
- [x] Translation binding and focus workflows available in inspector/manager.

## Docs and handoff

- [x] User guide present: `docs/user-guide.md`
- [x] Contributor guide present: `docs/contributor-guide.md`
- [x] Troubleshooting guide present: `docs/troubleshooting.md`
- [x] Release notes template present: `docs/release-notes-template.md`
- [x] Performance baseline and smoke checklist present: `docs/performance-baseline.md`

## Final manual QA

- Use `docs/release-qa-evidence.md` to capture reviewer, sample set, and pass/fail notes.
- [x] Run large-project smoke checklist in `docs/performance-baseline.md`.
- [x] Verify import/export roundtrip on current production project samples.
- [x] Tag release branch/commit after QA signoff.

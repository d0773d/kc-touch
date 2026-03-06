# YamUI Companion App — Finish Checklist

_Last updated: March 6, 2026_

This checklist is the execution plan to bring the Yam UI Generator to a releasable "finished" state.

## Milestone 1 — Live Preview Runtime Loop

### Scope
- Add a real preview loop that renders current editor project state with low-latency updates.
- Show preview health and render errors without blocking editor workflows.

### Exit Criteria
- Preview updates on project edits without manual refresh.
- Render failures surface actionable diagnostics (path + message).
- Preview mode handles at least one medium-size project without UI freeze.

### Done When
- Frontend integration test validates edit -> preview update flow.
- Backend contract (if used) is documented and versioned.

## Milestone 2 — Snapshot UX Completion

### Scope
- Add snapshot labels/notes.
- Add pin/unpin so pinned snapshots are retained.
- Add optional diff preview before restore confirmation.

### Exit Criteria
- Users can identify and restore meaningful checkpoints safely.
- Pinned snapshots are not dropped by rolling retention.
- Restore flow includes clear warning and confirmation.

### Done When
- Unit tests cover pin retention and restore target selection.
- Toolbar restore flow remains fully keyboard accessible.

## Milestone 3 — Generator and Validator Parity Hardening

### Scope
- Enforce complete cross-reference validation: screens, widgets, styles, assets, translations, bindings, events.
- Ensure export/import round-trip does not lose supported project data.

### Exit Criteria
- Round-trip test fixtures preserve semantic project state.
- Issue messages are consistent, categorized, and mappable to UI.
- Validation severities are stable and documented.

### Done When
- Backend test matrix includes positive + negative fixtures for each resource type.
- Frontend issue surfaces can navigate to all major finding types.

## Milestone 4 — Backend Contract and Migration Safety

### Scope
- Define API/schema versioning strategy.
- Add migration path for older saved project shapes.
- Standardize backend error envelope.

### Exit Criteria
- Older project files load with migration warnings, not crashes.
- Contract changes are explicit and backward-compatibility tested.

### Done When
- Migration tests cover at least two historical project versions.
- API docs include examples for success and error payloads.

## Milestone 5 — Test and CI Completion

### Scope
- Expand frontend integration tests for end-to-end editing flows.
- Expand backend edge-case tests (large payload, invalid assets, malformed translations).
- Add CI for frontend tests, backend tests, lint, and type checks.

### Exit Criteria
- CI runs on PR and blocks merge on failures.
- Core user flow has deterministic test coverage.

### Done When
- Green CI on default branch for 7 consecutive days.
- Documented local command parity with CI steps.

## Milestone 6 — Performance and Reliability Pass

### Scope
- Profile large project behavior and remove obvious hotspots.
- Add guardrails for expensive operations (debounce, memoization, capped scans).

### Exit Criteria
- Editor interactions remain responsive under representative large project load.
- No known crashers in import/export/restore flows.

### Done When
- Performance baseline and post-pass numbers are documented.
- Regression checklist includes large-project smoke test.

## Milestone 7 — Release Readiness

### Scope
- Final docs: user guide, contributor guide, troubleshooting, release notes template.
- Final pass on UX text consistency and error clarity.

### Exit Criteria
- New contributor can run backend/frontend from docs without assistance.
- Release checklist is repeatable and owned.

### Done When
- Taggable release candidate branch created.
- Manual QA checklist signed off.

## Cross-Milestone Rules

- Keep changes incremental and PR-sized.
- Preserve import/export compatibility unless an explicit migration is provided.
- Add tests with each milestone; avoid end-loaded testing.
- Prefer reversible feature flags for high-risk UI/runtime changes.

## Suggested Execution Order

1. Milestone 1 — Live Preview Runtime Loop
2. Milestone 2 — Snapshot UX Completion
3. Milestone 3 — Generator and Validator Parity Hardening
4. Milestone 4 — Backend Contract and Migration Safety
5. Milestone 5 — Test and CI Completion
6. Milestone 6 — Performance and Reliability Pass
7. Milestone 7 — Release Readiness

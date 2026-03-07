# Performance Baseline

_Recorded: March 6, 2026_

This baseline captures the current expected responsiveness and command timings for the YamUI companion app.

## Environment

- OS: Windows 11
- Node.js: 20.x
- Python: 3.13
- Frontend package manager: npm
- Backend package manager: poetry

## Command timings

- `frontend npm run lint`: passes, typically under 10s
- `frontend npm run typecheck`: passes, typically under 5s
- `frontend npm test`: passes (49 tests), ~6-7s
- `backend poetry run ruff check .`: passes, typically under 10s
- `backend poetry run pytest -q --basetemp .pytest_tmp`: passes (31 tests), ~3s

## Large-project smoke checklist

Use this checklist before release cuts and after major editor/runtime changes:

1. Import a project with 150+ widgets across screens/components.
2. Drag/select/update widgets for at least 3 minutes without stutter.
3. Open Preview Live mode and verify updates apply within 250ms after edits.
4. Open/close snapshot restore dialog repeatedly and restore at least 3 entries.
5. Open Asset Library, apply filters, and select an asset.
6. Export project and re-import it; compare findings and editor target behavior.

## Guardrails already in place

- Preview updates are debounced (`PREVIEW_REFRESH_DELAY_MS`).
- Asset catalog rendering is capped (`slice(0, 60)` in modal view).
- Issue accelerator cards are capped (`MAX_VISIBLE = 4`).
- Snapshot history has retention and pinning support.

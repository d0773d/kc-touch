# YamUI API Contract

This document captures stable API contract guarantees for the generator backend.

## Version Discovery

- `GET /contract` returns:
  - `api_version` (currently `1.0`)
  - `schema_version` (currently `2020-12`)
  - `migration_support` (legacy shapes accepted during import)

## Error Envelope

All non-success API responses use a consistent envelope:

```json
{
  "error": {
    "code": "bad_request",
    "message": "Human-readable error message",
    "status": 400
  }
}
```

Validation errors include `details`:

```json
{
  "error": {
    "code": "validation_error",
    "message": "Request validation failed",
    "status": 422,
    "details": []
  }
}
```

## Error Codes

- `bad_request`: Invalid request payload or domain validation failure (`400`)
- `not_found`: Referenced resource not found (`404`)
- `validation_error`: Request model/schema validation failed (`422`)
- `request_error`: Generic request failure for other 4xx statuses
- `internal_error`: Unhandled server failures (`500+`)

## Import Migration Guarantees

`POST /projects/import` accepts known legacy shapes and returns migration warnings in `issues`:

- Legacy top-level app keys (`initial_screen`, `locale`, `supported_locales`)
- Legacy `screens` array format
- Legacy translation maps without `entries`
- Legacy translation `values` arrays

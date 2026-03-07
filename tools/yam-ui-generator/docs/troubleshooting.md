# Troubleshooting

## Frontend cannot reach backend

- Ensure backend is running on port `8000`.
- Check `VITE_API_BASE_URL` if using a non-default URL.

## Import fails

- Validate YAML root is a mapping object.
- If importing older projects, inspect returned migration warnings.

## Validation warnings keep appearing

- Use Issue Accelerators to jump to affected widget/style paths.
- Check translation keys, style references, and event/binding state references.

## Asset issues

- Ensure image widgets have `src`.
- Avoid `..` in asset paths.
- If `app.asset_manifest` is set, referenced assets must exist in the manifest.

## CI failures

- Run the same commands locally listed in `tools/yam-ui-generator/README.md`.
- Backend lint gate is `ruff`; frontend lint gate is `eslint`.

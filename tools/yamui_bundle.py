#!/usr/bin/env python3
"""YamUI bundle generator.

Collects YAML schema fragments, merges them deterministically, and emits a single
runtime bundle plus a manifest suitable for OTA/version tracking. This implements
stages 1-8 of docs/YamUI/29-build-and-deployment-pipeline.md in code.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import sys
from copy import deepcopy
from pathlib import Path
from typing import Any, Dict, List

try:
    import yaml  # type: ignore
except ImportError as exc:  # pragma: no cover - failure path
    raise SystemExit(
        "PyYAML is required to build YamUI bundles. Install it with 'pip install pyyaml'."
    ) from exc

class YamuiDumper(yaml.SafeDumper):
    """Custom dumper that always indents block sequences."""

    def increase_indent(self, flow: bool = False, indentless: bool = False):  # type: ignore[override]
        return super().increase_indent(flow, False)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate a YamUI bundle from schema sources.")
    parser.add_argument(
        "--input",
        action="append",
        dest="inputs",
        help="Directory containing YAML schema fragments (can be repeated).",
    )
    parser.add_argument(
        "--output-yaml",
        required=True,
        type=Path,
        help="Path to write the merged YAML bundle.",
    )
    parser.add_argument(
        "--output-manifest",
        required=True,
        type=Path,
        help="Path to write the bundle manifest JSON.",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path.cwd(),
        help="Project root used for relative paths in the manifest.",
    )
    return parser.parse_args()


def _gather_files(inputs: List[str]) -> List[Path]:
    roots: List[Path] = [Path(p) for p in inputs] if inputs else []
    if not roots:
        raise SystemExit("No schema directories supplied to yamui_bundle.py")

    files: List[Path] = []
    for root in roots:
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*.yml")):
            files.append(path)
        for path in sorted(root.rglob("*.yaml")):
            files.append(path)
    files = sorted(set(files))
    if not files:
        raise SystemExit("No YAML schema files found in supplied directories")
    return files


def _merge_values(existing: Any, incoming: Any, context: str) -> Any:
    if existing is None:
        return deepcopy(incoming)
    if incoming is None:
        return existing

    if isinstance(existing, dict) and isinstance(incoming, dict):
        merged: Dict[str, Any] = dict(existing)
        for key, value in incoming.items():
            if key in merged:
                merged[key] = _merge_values(merged[key], value, f"{context}.{key}")
            else:
                merged[key] = deepcopy(value)
        return merged

    if isinstance(existing, list) and isinstance(incoming, list):
        merged_list = list(existing)
        merged_list.extend(deepcopy(incoming))
        return merged_list

    # Scalars or mismatched types -> incoming overrides per spec.
    return deepcopy(incoming)


def _load_yaml(path: Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = yaml.safe_load(handle)  # type: ignore[no-untyped-call]
    if data is None:
        return {}
    if not isinstance(data, dict):
        raise SystemExit(f"Schema file {path} must contain a mapping at the root")
    return data


def _write_yaml_bundle(destination: Path, payload: Dict[str, Any]) -> str:
    destination.parent.mkdir(parents=True, exist_ok=True)
    yaml_text = yaml.dump(  # type: ignore[no-untyped-call]
        payload,
        Dumper=YamuiDumper,
        sort_keys=False,
        allow_unicode=True,
        default_flow_style=False,
    )
    destination.write_text(yaml_text, encoding="utf-8")
    return yaml_text


def _write_manifest(destination: Path, payload: Dict[str, Any]) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")


def main() -> None:
    args = _parse_args()
    sources = _gather_files(args.inputs or [])

    bundle: Dict[str, Any] = {}
    source_records: List[Dict[str, Any]] = []
    project_root = args.root.resolve()

    for path in sources:
        data = _load_yaml(path)
        bundle = _merge_values(bundle, data, context=path.name)
        abs_path = path.resolve()
        try:
            relative = str(abs_path.relative_to(project_root))
        except ValueError:
            relative = str(abs_path)
        source_records.append({
            "path": relative,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        })

    yaml_text = _write_yaml_bundle(args.output_yaml, bundle)
    checksum = hashlib.sha256(yaml_text.encode("utf-8")).hexdigest()

    generated_at = _dt.datetime.now(_dt.UTC).isoformat(timespec="seconds").replace("+00:00", "Z")

    manifest = {
        "generated_at": generated_at,
        "version": bundle.get("version", 1),
        "sources": source_records,
        "counts": {
            "styles": len(bundle.get("styles", {}) or {}),
            "components": len(bundle.get("components", {}) or {}),
            "sensor_templates": len(bundle.get("sensor_templates", {}) or {}),
            "state_keys": len(bundle.get("state", {}) or {}),
        },
        "checksum": checksum,
    }
    _write_manifest(args.output_manifest, manifest)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:  # pragma: no cover
        sys.exit(130)

#!/usr/bin/env python3
"""Clone and verify the exact external revisions used by DKR-R 1.0.2."""
from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess
import sys
from typing import Any

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCK_FILE = ROOT / "dependencies.lock.json"
RESOLVED_FILE = ROOT / "extern" / ".resolved-dependencies.json"


def run(command: list[str], cwd: pathlib.Path | None = None, capture: bool = False) -> str:
    print("+", " ".join(command))
    result = subprocess.run(
        command,
        cwd=str(cwd) if cwd else None,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )
    if result.returncode != 0:
        if capture and result.stdout:
            print(result.stdout, file=sys.stderr)
        raise RuntimeError(f"Command failed with exit code {result.returncode}: {' '.join(command)}")
    return (result.stdout or "").strip()


def ensure_tool(name: str) -> None:
    if shutil.which(name) is None:
        raise RuntimeError(f"Required tool was not found on PATH: {name}")


def is_dirty(directory: pathlib.Path) -> bool:
    return bool(run(["git", "status", "--porcelain"], cwd=directory, capture=True))


def prepare_dependency(entry: dict[str, Any], force: bool) -> dict[str, str]:
    destination = ROOT / entry["destination"]
    repository = entry["repository"]
    commit = entry["commit"]
    recursive = bool(entry.get("recursive", False))

    if destination.exists() and not (destination / ".git").exists():
        if any(destination.iterdir()):
            raise RuntimeError(f"{destination} exists but is not a Git checkout. Move or remove it first.")
        destination.rmdir()

    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--filter=blob:none", "--no-checkout", repository, str(destination)])
    else:
        current_remote = run(["git", "remote", "get-url", "origin"], cwd=destination, capture=True)
        if current_remote.rstrip("/").removesuffix(".git").lower() != repository.rstrip("/").removesuffix(".git").lower():
            raise RuntimeError(f"Unexpected origin for {entry['name']}: {current_remote}")
        if is_dirty(destination) and not force:
            raise RuntimeError(
                f"{entry['name']} contains local changes. Commit/stash them or rerun with --force to discard them."
            )
        if force and is_dirty(destination):
            run(["git", "reset", "--hard"], cwd=destination)
            run(["git", "clean", "-fd"], cwd=destination)

    run(["git", "fetch", "--no-tags", "origin", commit], cwd=destination)
    run(["git", "checkout", "--detach", commit], cwd=destination)
    if recursive:
        run(["git", "submodule", "update", "--init", "--recursive"], cwd=destination)

    resolved = run(["git", "rev-parse", "HEAD"], cwd=destination, capture=True)
    if resolved.lower() != commit.lower():
        raise RuntimeError(f"Resolved revision mismatch for {entry['name']}: expected {commit}, got {resolved}")
    return {"name": entry["name"], "commit": resolved, "destination": entry["destination"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", action="append", help="Prepare only this dependency name (repeatable).")
    parser.add_argument("--force", action="store_true", help="Discard local changes in managed dependency folders.")
    args = parser.parse_args()

    ensure_tool("git")
    lock = json.loads(LOCK_FILE.read_text(encoding="utf-8"))
    selected = set(args.only or [])
    entries = [item for item in lock["dependencies"] if not selected or item["name"] in selected]
    missing = selected.difference(item["name"] for item in entries)
    if missing:
        raise RuntimeError("Unknown dependency name(s): " + ", ".join(sorted(missing)))

    resolved = [prepare_dependency(entry, args.force) for entry in entries]
    RESOLVED_FILE.parent.mkdir(parents=True, exist_ok=True)
    RESOLVED_FILE.write_text(json.dumps({"schemaVersion": 1, "dependencies": resolved}, indent=2) + "\n", encoding="utf-8")
    print(f"\nPrepared {len(resolved)} dependency checkout(s).")
    print(f"Resolution record: {RESOLVED_FILE.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, KeyError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)

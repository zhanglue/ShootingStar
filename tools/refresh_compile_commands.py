#!/usr/bin/env python3

import json
import re
import subprocess
import sys
from pathlib import Path


SOURCE_RE = re.compile(r"^(src|tests|protos)/.*\.(c|cc|cpp|cxx)$")
WORKSPACE_HEADER_RE = re.compile(r"^(src|tests|protos)/.*\.(h|hh|hpp|inc)$")
QUERY = 'mnemonic("CppCompile", set(//src/... //tests/... //protos/...))'
TARGET_PATTERNS = ["//src/...", "//tests/...", "//protos/..."]
PROTOBUF_SRC_FLAGS = [
    "-isystem",
    "external/protobuf+/src",
    "-isystem",
    "bazel-out/darwin_arm64-fastbuild/bin/external/protobuf+/src",
]
CLANGD_DRIVER = "/usr/bin/clang++"
PATH_FLAGS = {"-iquote", "-isystem", "-MF", "-o"}


def workspace_root() -> Path:
    return Path(__file__).resolve().parent.parent


def command_output(command: list[str]) -> str:
    return subprocess.run(
        command,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def apple_sdk_flags() -> list[str]:
    cached = getattr(apple_sdk_flags, "_cached", None)
    if cached is not None:
        return cached

    sdk_path = command_output(["xcrun", "--show-sdk-path"])
    resource_dir = command_output([CLANGD_DRIVER, "-print-resource-dir"])
    flags = [
        "-isysroot",
        sdk_path,
        "-resource-dir",
        resource_dir,
    ]
    apple_sdk_flags._cached = flags
    return flags


def external_include_flags(output_base: str) -> list[str]:
    cached = getattr(external_include_flags, "_cached", None)
    if cached is not None:
        return cached

    flags = [
        "-isystem",
        str(Path(output_base) / "external/grpc+/include"),
        "-isystem",
        str(Path(output_base) / "external/protobuf+/src"),
        "-isystem",
        str(Path(output_base) / "external/googletest+/googletest/include"),
        "-isystem",
        str(Path(output_base) / "external/googletest+/googlemock/include"),
    ]
    external_include_flags._cached = flags
    return flags


def execroot_dir(root: Path) -> str:
    cached = getattr(execroot_dir, "_cached", None)
    if cached is not None:
        return cached
    value = command_output(["bazel", "info", "execution_root"])
    execroot_dir._cached = value
    return value


def output_base_dir() -> str:
    cached = getattr(output_base_dir, "_cached", None)
    if cached is not None:
        return cached
    value = command_output(["bazel", "info", "output_base"])
    output_base_dir._cached = value
    return value


def normalize_path(path: str, execroot: str, output_base: str) -> str:
    if path.startswith("external/"):
        return str(Path(output_base) / path)
    if path.startswith("bazel-out/"):
        return str(Path(execroot) / path)
    return path


def normalize_arguments(arguments: list[str], execroot: str, output_base: str) -> list[str]:
    normalized = []
    index = 0

    while index < len(arguments):
        argument = arguments[index]

        if argument in PATH_FLAGS and index + 1 < len(arguments):
            normalized.append(argument)
            normalized.append(normalize_path(arguments[index + 1], execroot, output_base))
            index += 2
            continue

        if argument.startswith("-I") and len(argument) > 2:
            normalized.append("-I" + normalize_path(argument[2:], execroot, output_base))
        elif argument.startswith("-frandom-seed="):
            prefix, value = argument.split("=", 1)
            normalized.append(prefix + "=" + normalize_path(value, execroot, output_base))
        else:
            normalized.append(argument)

        index += 1

    return normalized


def source_from_arguments(arguments: list[str]) -> str | None:
    for arg in reversed(arguments):
        if SOURCE_RE.match(arg):
            return arg
    return None


def with_extra_flags(arguments: list[str]) -> list[str]:
    execroot = execroot_dir(workspace_root())
    output_base = output_base_dir()
    normalized = normalize_arguments(arguments, execroot, output_base)
    if normalized:
        normalized[0] = CLANGD_DRIVER

    sdk_flags = apple_sdk_flags()
    external_flags = external_include_flags(output_base)
    for idx in range(0, len(normalized) - 1):
        if normalized[idx:idx + 2] == sdk_flags[:2]:
            break
    else:
        normalized.extend(sdk_flags)

    for idx in range(0, len(normalized) - 1):
        if normalized[idx:idx + 2] == external_flags[:2]:
            break
    else:
        normalized.extend(external_flags)

    for idx in range(0, len(normalized) - 3):
        if normalized[idx:idx + 4] == PROTOBUF_SRC_FLAGS:
            return normalized
    return normalized + PROTOBUF_SRC_FLAGS


def build_path_fragment_map(data: dict) -> dict[int, tuple[str, int | None]]:
    return {
        fragment["id"]: (fragment["label"], fragment.get("parentId"))
        for fragment in data.get("pathFragments", [])
    }


def build_artifact_map(data: dict, path_fragment_map: dict[int, tuple[str, int | None]]) -> dict[int, str]:
    def resolve(fragment_id: int) -> str:
        labels = []
        current = fragment_id
        while current is not None:
            label, parent_id = path_fragment_map[current]
            labels.append(label)
            current = parent_id
        return "/".join(reversed(labels))

    return {
        artifact["id"]: resolve(artifact["pathFragmentId"])
        for artifact in data.get("artifacts", [])
    }


def build_depset_map(data: dict) -> dict[int, dict]:
    return {depset["id"]: depset for depset in data.get("depSetOfFiles", [])}


def workspace_headers_for_action(
    action: dict, artifact_map: dict[int, str], depset_map: dict[int, dict]
) -> list[str]:
    header_paths = set()
    stack = list(action.get("inputDepSetIds", []))
    visited = set()

    while stack:
        depset_id = stack.pop()
        if depset_id in visited:
            continue
        visited.add(depset_id)

        depset = depset_map.get(depset_id, {})
        for direct_id in depset.get("directArtifactIds", []):
            path = artifact_map.get(direct_id)
            if path and WORKSPACE_HEADER_RE.match(path):
                header_paths.add(path)
        stack.extend(depset.get("transitiveDepSetIds", []))

    return sorted(header_paths)


def build_entries(root: Path, data: dict) -> list[dict]:
    directory = execroot_dir(root)
    path_fragment_map = build_path_fragment_map(data)
    artifact_map = build_artifact_map(data, path_fragment_map)
    depset_map = build_depset_map(data)
    entries = []
    seen = set()

    for action in data.get("actions", []):
        arguments = action.get("arguments", [])
        source_rel = source_from_arguments(arguments)
        if not source_rel:
            continue

        normalized_arguments = with_extra_flags(arguments)
        source_abs = str(root / source_rel)
        header_paths = [str(root / path) for path in workspace_headers_for_action(action, artifact_map, depset_map)]

        for file_path in [source_abs, *header_paths]:
            if file_path in seen:
                continue
            seen.add(file_path)
            entries.append(
                {
                    "directory": directory,
                    "file": file_path,
                    "arguments": normalized_arguments,
                }
            )

    entries.sort(key=lambda entry: entry["file"])
    return entries


def main() -> int:
    root = workspace_root()
    output_path = root / "compile_commands.json"

    try:
        subprocess.run(
            ["bazel", "build", "--nobuild", *TARGET_PATTERNS],
            cwd=root,
            check=True,
            capture_output=True,
            text=True,
        )

        result = subprocess.run(
            ["bazel", "aquery", "--output=jsonproto", QUERY],
            cwd=root,
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as exc:
        sys.stderr.write(exc.stderr)
        return exc.returncode

    data = json.loads(result.stdout)
    entries = build_entries(root, data)
    output_path.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(entries)} entries to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

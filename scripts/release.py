#!/usr/bin/env python3
"""Plugin releases and channel release sets.

  release.py tag <plugin>/vX.Y.Z
  release.py pack --build-dir DIR --target ARCH --out DIR <plugin>...
  release.py unpack --artifacts DIR --target ARCH --into DIR <plugin>...
  release.py validate --build-dir DIR --out DIR <plugin>...
  release.py entry --out DIR --entries DIR --entries DIR <plugin>
  release.py roster <channel>
  release.py index --channel C --entries DIR --out FILE
  release.py manifest --channel C --version N --artifacts DIR --out FILE

A plugin tag is released on its own: tag checks the tag names a plugin and a version. pack writes
one <plugin>-<target>.tar.zst: the .so, its config template and, when the build put one beside the
.so, data/. unpack lays archives out as a build tree again, so validate checks what ships rather
than what was built: the publish gate, then the plugin's smoke.toml. entry keeps the gate's index
entry, which must agree across targets.

A channel release set only gathers: roster prints the version channels/<channel>/roster.toml pins
for each plugin, and index and manifest are written over the pinned plugins' released artifacts.

Exit status: 0 when everything passed, 1 when something failed, 2 on a usage error.
"""

import argparse
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent

# build.sh --target names to the release targets the frontend asks the channel for.
TARGETS = {"x86_64": "linux-x86_64", "aarch64": "linux-arm64"}
# A plugin release's tag is <plugin>/v<VERSION>.
VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?")
INDEX_NAME = "plugin-index"
INDEX_PATH = "plugin-index.json"
SCHEMA_VERSION = 1
# Changing the level changes every archive's bytes, and so every digest.
ZSTD_LEVEL = 19


class ReleaseError(Exception):
    pass


def archive_name(plugin, target):
    return f"{plugin}-{TARGETS[target]}.tar.zst"


def check_plugin(plugin):
    if not re.fullmatch(r"[a-z0-9_]+", plugin) or not (REPO_DIR / "plugins" / plugin / "CMakeLists.txt").is_file():
        raise ReleaseError(f"'{plugin}' is not a plugin in this repository")


def plugin_tag(plugin, version):
    return f"{plugin}/v{version}"


def parse_tag(tag):
    plugin, _, version = tag.partition("/v")
    if not VERSION.fullmatch(version):
        raise ReleaseError(f"tag '{tag}' is not <plugin>/vX.Y.Z")
    check_plugin(plugin)
    return plugin, version


def roster(channel):
    """(plugin, version) pairs in roster order."""
    import tomllib

    path = REPO_DIR / "channels" / channel / "roster.toml"
    if not path.is_file():
        raise ReleaseError(f"no roster for channel '{channel}' at {path}")
    pins = tomllib.loads(path.read_text(encoding="utf-8")).get("plugins", {})
    if not pins:
        raise ReleaseError(f"{path} pins no plugins")
    for plugin, version in pins.items():
        check_plugin(plugin)
        if not isinstance(version, str) or not VERSION.fullmatch(version):
            raise ReleaseError(f"{path}: {plugin} = {version!r} is not a version X.Y.Z")
    return list(pins.items())


def archive_members(plugin, build_dir):
    """(name in the archive, file on disk) for everything a plugin ships, directories included."""
    built = build_dir / "plugins" / plugin
    artifact = built / f"{plugin}.so"
    if not artifact.is_file():
        raise ReleaseError(f"{plugin}: no built plugin at {artifact}")
    templates = sorted((REPO_DIR / "plugins" / plugin).glob("*.json5"))
    if len(templates) != 1:
        raise ReleaseError(f"{plugin}: plugins/{plugin} must hold exactly one .json5 template")
    members = [(artifact.name, artifact), (templates[0].name, templates[0])]
    data = built / "data"
    if data.is_dir():
        members.append(("data", data))
        members += [(path.relative_to(built).as_posix(), path) for path in data.rglob("*")]
    return sorted(members)


def tar_bytes(members):
    """A tar whose bytes depend only on the names and contents: no times, owners or umask."""
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode="w", format=tarfile.PAX_FORMAT) as tar:
        for name, path in members:
            info = tarfile.TarInfo(name)
            info.mtime = 0
            if path.is_symlink():
                raise ReleaseError(f"{path} is a link, which the frontend refuses to unpack")
            if path.is_dir():
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                tar.addfile(info)
            elif path.is_file():
                info.size = path.stat().st_size
                info.mode = 0o755 if name.endswith(".so") else 0o644
                with path.open("rb") as data:
                    tar.addfile(info, data)
            else:
                raise ReleaseError(f"{path} is neither a regular file nor a directory")
    return out.getvalue()


def pack(args):
    args.out.mkdir(parents=True, exist_ok=True)
    for plugin in args.plugins:
        packed = subprocess.run(
            ["zstd", f"-{ZSTD_LEVEL}", "-T1", "-q", "-c"],
            input=tar_bytes(archive_members(plugin, args.build_dir)),
            capture_output=True,
            check=True,
        ).stdout
        path = args.out / archive_name(plugin, args.target)
        path.write_bytes(packed)
        print(f"release: packed {path.name}, {hashlib.sha256(packed).hexdigest()}")


def unpack(args):
    for plugin in args.plugins:
        archive = args.artifacts / archive_name(plugin, args.target)
        if not archive.is_file():
            raise ReleaseError(f"{plugin}: no {archive.name} in {args.artifacts}")
        raw = subprocess.run(["zstd", "-d", "-q", "-c", str(archive)], capture_output=True, check=True).stdout
        dest = args.into / "plugins" / plugin
        dest.mkdir(parents=True)
        with tarfile.open(fileobj=io.BytesIO(raw)) as tar:
            # What the frontend's unpack refuses: links, devices, and paths leaving the directory.
            for member in tar.getmembers():
                parts = Path(member.name).parts
                if not (member.isfile() or member.isdir()) or member.name.startswith("/") or ".." in parts:
                    raise ReleaseError(f"{archive.name}: '{member.name}' is not allowed in a plugin archive")
            tar.extractall(dest)


def validate(args):
    failed = []
    gate = subprocess.run(
        [str(REPO_DIR / "scripts" / "publish_gate.py"), "check", "--build-dir", str(args.build_dir), "--out", str(args.out)]
        + args.plugins
    )
    if gate.returncode != 0:
        failed.append("the publish gate")
    smoke_host = (args.build_dir / "smoke" / "replay_smoke").resolve()
    for plugin in args.plugins:
        config = REPO_DIR / "plugins" / plugin / "smoke.toml"
        if not config.is_file():
            print(f"release: FAIL - {plugin}: no smoke.toml, and a rostered plugin must have one", file=sys.stderr)
            failed.append(f"{plugin}'s smoke")
            continue
        artifact = (args.build_dir / "plugins" / plugin / f"{plugin}.so").resolve()
        # Some emulators write their settings into the working directory.
        with tempfile.TemporaryDirectory(prefix=f"smoke-{plugin}-") as scratch:
            smoke = subprocess.run([str(smoke_host), str(artifact), str(config)], cwd=scratch)
        if smoke.returncode != 0:
            print(f"release: FAIL - {plugin}: smoke", file=sys.stderr)
            failed.append(f"{plugin}'s smoke")
    if failed:
        raise ReleaseError(f"failed: {', '.join(failed)}")
    print(f"release: {len(args.plugins)} plugins passed the gate and their smoke")


def read_entry(path):
    if not path.is_file():
        raise ReleaseError(f"no gate entry at {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def entry(args):
    entries = [read_entry(directory / f"{args.plugin}.json") for directory in args.entries]
    # The index is one per release set, so every target has to describe the plugin identically.
    if any(other != entries[0] for other in entries):
        raise ReleaseError(f"{args.plugin}: the gate entries differ between {', '.join(map(str, args.entries))}")
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / f"{args.plugin}.json").write_text(json.dumps(entries[0], indent=2) + "\n", encoding="utf-8")


def index(args):
    plugins = [read_entry(args.entries / f"{plugin}.json") for plugin, _ in roster(args.channel)]
    args.out.write_text(json.dumps({"schema": SCHEMA_VERSION, "plugins": plugins}, indent=2) + "\n", encoding="utf-8")


def git(*command, env=None):
    return subprocess.run(
        ["git", "-C", str(REPO_DIR), *command], capture_output=True, text=True, check=True, env=env
    ).stdout.strip()


def manifest_artifact(name, target, path, revision, version=None):
    data = path.read_bytes()
    artifact = {"name": name}
    if target:
        artifact["target"] = target
    artifact.update(path=path.name, sha256=hashlib.sha256(data).hexdigest(), size=len(data), revision=revision)
    if version:
        artifact["version"] = version
    return artifact


def manifest(args):
    revision = git("rev-parse", "--verify", "HEAD^{commit}")
    # The commit's own time rather than the clock, so a re-run of a tag writes the same manifest.
    published = git(
        "show", "-s", "--date=format-local:%Y-%m-%dT%H:%M:%SZ", "--format=%cd", revision, env={**os.environ, "TZ": "UTC"}
    )
    index_file = args.artifacts / INDEX_PATH
    if not index_file.is_file():
        raise ReleaseError(f"no {INDEX_PATH} in {args.artifacts}")
    artifacts = [manifest_artifact(INDEX_NAME, None, index_file, revision)]
    for plugin, version in roster(args.channel):
        tag = plugin_tag(plugin, version)
        try:
            built_from = git("rev-list", "-n", "1", f"refs/tags/{tag}")
        except subprocess.CalledProcessError:
            raise ReleaseError(f"{plugin}: the roster pins {version}, but there is no tag {tag}") from None
        for target in TARGETS:
            path = args.artifacts / archive_name(plugin, target)
            if not path.is_file():
                raise ReleaseError(f"{plugin}: no {path.name} in {args.artifacts}")
            artifacts.append(manifest_artifact(plugin, TARGETS[target], path, built_from, version))
    artifacts.sort(key=lambda artifact: (artifact["name"], artifact.get("target", "")))
    document = {
        "schema": SCHEMA_VERSION,
        "version": args.version,
        "source_revision": revision,
        "published": published,
        "artifacts": artifacts,
    }
    data = (json.dumps(document, indent=2) + "\n").encode()
    args.out.write_bytes(data)
    print(hashlib.sha256(data).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    tag_parser = commands.add_parser("tag", help="print the plugin and version a plugin tag names")
    tag_parser.add_argument("tag")

    pack_parser = commands.add_parser("pack", help="write each plugin's .tar.zst")
    pack_parser.add_argument("--build-dir", type=Path, required=True)
    pack_parser.add_argument("--target", choices=TARGETS, required=True)
    pack_parser.add_argument("--out", type=Path, required=True)
    pack_parser.add_argument("plugins", nargs="+")

    unpack_parser = commands.add_parser("unpack", help="lay packed plugins out as a build tree")
    unpack_parser.add_argument("--artifacts", type=Path, required=True)
    unpack_parser.add_argument("--target", choices=TARGETS, required=True)
    unpack_parser.add_argument("--into", type=Path, required=True)
    unpack_parser.add_argument("plugins", nargs="+")

    validate_parser = commands.add_parser("validate", help="the publish gate and the smoke, for every plugin")
    validate_parser.add_argument("--build-dir", type=Path, required=True)
    validate_parser.add_argument("--out", type=Path, required=True, help="where the gate writes index entries")
    validate_parser.add_argument("plugins", nargs="+")

    entry_parser = commands.add_parser("entry", help="keep the gate's entry once every target agrees on it")
    entry_parser.add_argument("--out", type=Path, required=True)
    entry_parser.add_argument("--entries", type=Path, action="append", required=True)
    entry_parser.add_argument("plugin")

    roster_parser = commands.add_parser("roster", help="print the plugin versions a channel pins")
    roster_parser.add_argument("channel")

    index_parser = commands.add_parser("index", help="join the pinned plugins' entries into the plugin index")
    index_parser.add_argument("--channel", required=True)
    index_parser.add_argument("--entries", type=Path, required=True)
    index_parser.add_argument("--out", type=Path, required=True)

    manifest_parser = commands.add_parser("manifest", help="write the release-set manifest, printing its digest")
    manifest_parser.add_argument("--channel", required=True)
    manifest_parser.add_argument("--version", type=int, required=True)
    manifest_parser.add_argument("--artifacts", type=Path, required=True)
    manifest_parser.add_argument("--out", type=Path, required=True)

    args = parser.parse_args()
    try:
        if args.command == "tag":
            print(*parse_tag(args.tag))
        elif args.command == "roster":
            for plugin, version in roster(args.channel):
                print(plugin, version)
        else:
            commands = {"pack": pack, "unpack": unpack, "validate": validate, "entry": entry, "index": index, "manifest": manifest}
            commands[args.command](args)
    except ReleaseError as error:
        print(f"release: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

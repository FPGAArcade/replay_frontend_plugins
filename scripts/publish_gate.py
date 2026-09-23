#!/usr/bin/env python3
"""The publish gate: what a built plugin must pass before it is signed.

  publish_gate.py check --build-dir DIR --out DIR <plugin>...
  publish_gate.py reproduce [--ref REF] <plugin>... [-- <build.sh options>]

check audits each built plugin and writes its plugin-index entry to <out>/<id>.json:

  template  the config template parses and carries what the index entry is built from
  load      dlopen succeeds on this machine's loader and the ABI version is the SDK's exactly
  exports   the entry point and ABI version and nothing else, within the glibc floor
  imports   nothing outside the host's export list and the platform's own libraries

It runs on the target it checks, since dlopen has to. A plugin that fails any check gets no
entry. reproduce builds <ref> twice from clean clones at two paths and fails unless every
artifact comes out byte-identical.

Exit status: 0 when everything passed, 1 when something failed, 2 on a usage error.
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent

# What the frontend does with a match and how strongly it wants it, as it records a sideloaded
# plugin; a template declares neither.
ACTION_RUN_DIRECTLY = "run_directly"
DEFAULT_PRIORITY = 50


class TemplateError(Exception):
    pass


class Json5Parser:
    """The JSON5 the config templates are written in: comments, unquoted keys, either quote,
    trailing commas, and a top level that may leave out its braces."""

    NUMBER = re.compile(r"[+-]?(0[xX][0-9a-fA-F]+|(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?|Infinity|NaN)")
    IDENTIFIER = re.compile(r"[A-Za-z_$][A-Za-z0-9_$]*")
    ESCAPES = {"b": "\b", "f": "\f", "n": "\n", "r": "\r", "t": "\t", "v": "\v", "0": "\0"}

    def __init__(self, text):
        self.text = text
        self.pos = 0

    def fail(self, message):
        line = self.text.count("\n", 0, self.pos) + 1
        raise TemplateError(f"line {line}: {message}")

    def skip(self):
        while self.pos < len(self.text):
            if self.text[self.pos].isspace():
                self.pos += 1
            elif self.text.startswith("//", self.pos):
                end = self.text.find("\n", self.pos)
                self.pos = len(self.text) if end < 0 else end
            elif self.text.startswith("/*", self.pos):
                end = self.text.find("*/", self.pos + 2)
                if end < 0:
                    self.fail("unterminated comment")
                self.pos = end + 2
            else:
                return

    def peek(self):
        self.skip()
        return self.text[self.pos] if self.pos < len(self.text) else ""

    def expect(self, char):
        if self.peek() != char:
            self.fail(f"expected '{char}'")
        self.pos += 1

    def document(self):
        if self.peek() == "{":
            value = self.value()
        else:
            value = self.members("")
        if self.peek():
            self.fail("unexpected text after the document")
        return value

    def members(self, close):
        result = {}
        while self.peek() != close:
            if not self.peek():
                self.fail("unexpected end of document")
            key = self.string() if self.peek() in "\"'" else self.identifier()
            if key in result:
                self.fail(f"key '{key}' set twice")
            self.expect(":")
            result[key] = self.value()
            if self.peek() != ",":
                break
            self.pos += 1
        return result

    def value(self):
        char = self.peek()
        if char == "{":
            self.pos += 1
            result = self.members("}")
            self.expect("}")
            return result
        if char == "[":
            self.pos += 1
            result = []
            while self.peek() != "]":
                result.append(self.value())
                if self.peek() != ",":
                    break
                self.pos += 1
            self.expect("]")
            return result
        if char in "\"'":
            return self.string()
        for word, literal in (("true", True), ("false", False), ("null", None)):
            if self.text.startswith(word, self.pos) and not self.IDENTIFIER.match(self.text, self.pos + len(word)):
                self.pos += len(word)
                return literal
        match = self.NUMBER.match(self.text, self.pos)
        if not match:
            self.fail("expected a value")
        self.pos = match.end()
        text = match.group(0)
        if "x" in text.lower():
            return int(text, 16)
        number = float(text)
        return int(number) if number.is_integer() and re.fullmatch(r"[+-]?\d+", text) else number

    def identifier(self):
        match = self.IDENTIFIER.match(self.text, self.pos)
        if not match:
            self.fail("expected a key")
        self.pos = match.end()
        return match.group(0)

    def string(self):
        quote = self.text[self.pos]
        self.pos += 1
        out = []
        while self.pos < len(self.text):
            char = self.text[self.pos]
            self.pos += 1
            if char == quote:
                return "".join(out)
            if char == "\n":
                self.fail("unterminated string")
            if char == "\\":
                escaped = self.text[self.pos : self.pos + 1]
                self.pos += 1
                if escaped == "u":
                    out.append(chr(int(self.text[self.pos : self.pos + 4], 16)))
                    self.pos += 4
                elif escaped != "\n":
                    out.append(self.ESCAPES.get(escaped, escaped))
            else:
                out.append(char)
        self.fail("unterminated string")


KIND_NAMES = {str: "a string", bool: "true or false", dict: "an object", list: "a list"}


def field(template, key, kind, default):
    value = template.get(key, default)
    if not isinstance(value, kind):
        raise TemplateError(f"'{key}' must be {KIND_NAMES[kind]}")
    return value


def string_list(template, key):
    values = field(template, key, list, [])
    if not all(isinstance(value, str) and value for value in values):
        raise TemplateError(f"'{key}' must hold non-empty strings")
    return values


def index_entry(template_path, plugin, abi_version):
    """The plugin-index entry for a template, keyed as the frontend's plugin index reads it."""
    template = Json5Parser(template_path.read_text(encoding="utf-8")).document()
    name = field(template, "name", str, "")
    if not name:
        raise TemplateError("no 'name'")
    # A sideload replaces the channel plugin whose id is its lowercased name, so the two must agree.
    if name.lower() != plugin:
        raise TemplateError(f"'name' {name!r} gives the id {name.lower()!r}, but the plugin is published as {plugin!r}")
    if field(template, "plugin", str, "") != f"{plugin}.so":
        raise TemplateError(f"'plugin' must name the built artifact, {plugin}.so")
    extensions = string_list(template, "extensions")
    if not extensions:
        raise TemplateError("'extensions' claims nothing")
    dotted = [extension for extension in extensions if extension.startswith(".")]
    if dotted:
        raise TemplateError(f"'extensions' are written without a leading dot: {', '.join(dotted)}")
    metadata = field(template, "metadata", dict, {})

    return {
        "id": plugin,
        "name": name,
        "platform": field(template, "platform", str, ""),
        "kind": "emulator",
        "description": field(metadata, "info", str, ""),
        "recommended": field(template, "recommended", bool, False),
        "abi_version": abi_version,
        "launch_helper": field(template, "launch_helper", str, ""),
        "data_artifacts": string_list(template, "data_artifacts"),
        "media_support": [
            {
                "extensions": extensions,
                "action": ACTION_RUN_DIRECTLY,
                "priority": DEFAULT_PRIORITY,
                "description": "",
            }
        ],
    }


def run(command):
    """Runs a check, returning (failure output or None, stdout)."""
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode == 0:
        return None, result.stdout
    return (result.stderr + result.stdout).strip() or f"{command[0]} exited {result.returncode}", result.stdout


def check_plugin(plugin, source_dir, build_dir, out_dir):
    """Every check for one plugin, returning its failures as (check, reason) pairs."""
    failures = []
    artifact = build_dir / "plugins" / plugin / f"{plugin}.so"
    if not artifact.is_file():
        return [("artifact", f"no built plugin at {artifact}")]

    abi_version = None
    load_failure, loaded = run([str(build_dir / "smoke" / "replay_smoke"), str(artifact), "--load-only"])
    if load_failure:
        failures.append(("load", load_failure))
    else:
        abi_version = int(re.search(r"ABI version (\d+)", loaded).group(1))

    for check, command in (
        ("exports", [str(REPO_DIR / "scripts" / "check_abi_floor.sh"), str(artifact)]),
        ("imports", [str(REPO_DIR / "sdk" / "scripts" / "check_plugin.sh"), str(artifact)]),
    ):
        failure, _ = run(command)
        if failure:
            failures.append((check, failure))

    templates = sorted((source_dir / "plugins" / plugin).glob("*.json5"))
    entry = None
    if len(templates) != 1:
        failures.append(("template", f"plugins/{plugin} must hold exactly one .json5 template, not {len(templates)}"))
    else:
        try:
            entry = index_entry(templates[0], plugin, abi_version)
        except TemplateError as error:
            failures.append(("template", f"{templates[0].name}: {error}"))

    fragment = out_dir / f"{plugin}.json"
    if failures:
        fragment.unlink(missing_ok=True)
    else:
        fragment.write_text(json.dumps(entry, indent=2) + "\n", encoding="utf-8")
    return failures


def check(args):
    smoke_host = args.build_dir / "smoke" / "replay_smoke"
    if not smoke_host.is_file():
        print(f"gate: no smoke host at {smoke_host}; build with ./build.sh first", file=sys.stderr)
        return 2
    args.out.mkdir(parents=True, exist_ok=True)

    failed = 0
    for plugin in args.plugins:
        failures = check_plugin(plugin, args.source_dir, args.build_dir, args.out)
        if failures:
            failed += 1
            for name, reason in failures:
                print(f"gate: FAIL - {plugin}: {name}", file=sys.stderr)
                for line in reason.splitlines():
                    print(f"    {line}", file=sys.stderr)
        else:
            print(f"gate: OK - {plugin}: index entry in {args.out / (plugin + '.json')}")
    print(f"gate: {len(args.plugins) - failed} passed, {failed} failed")
    return 1 if failed else 0


def reproduce(args):
    ref = subprocess.run(
        ["git", "-C", str(REPO_DIR), "rev-parse", "--verify", f"{args.ref}^{{commit}}"],
        capture_output=True,
        text=True,
    )
    if ref.returncode != 0:
        print(f"gate: no such commit: {args.ref}", file=sys.stderr)
        return 2
    commit = ref.stdout.strip()

    digests = []
    with tempfile.TemporaryDirectory(prefix="publish-gate-") as scratch:
        # Two different paths, since the checkout path is what file-prefix mapping has to absorb.
        for clone in (Path(scratch) / "first", Path(scratch) / "second" / "checkout"):
            subprocess.run(["git", "clone", "--quiet", "--shared", "--no-checkout", str(REPO_DIR), str(clone)], check=True)
            subprocess.run(["git", "-C", str(clone), "checkout", "--quiet", "--detach", commit], check=True)
            build = subprocess.run(
                ["./build.sh", *args.plugins, "release", *args.build_options], cwd=clone, capture_output=True, text=True
            )
            if build.returncode != 0:
                print(f"gate: FAIL - the build in {clone} failed:", file=sys.stderr)
                print(build.stdout + build.stderr, file=sys.stderr)
                return 1
            built = {}
            for plugin in args.plugins:
                artifacts = list(clone.glob(f"build/*/plugins/{plugin}/{plugin}.so"))
                if len(artifacts) != 1:
                    print(f"gate: FAIL - {plugin}: expected one artifact in {clone}/build, found {len(artifacts)}", file=sys.stderr)
                    return 1
                built[plugin] = hashlib.sha256(artifacts[0].read_bytes()).hexdigest()
            digests.append(built)

    failed = 0
    for plugin in args.plugins:
        first, second = digests[0][plugin], digests[1][plugin]
        if first == second:
            print(f"gate: OK - {plugin}: two clean builds of {commit[:12]} are identical, {first}")
        else:
            failed += 1
            print(f"gate: FAIL - {plugin}: two clean builds of {commit[:12]} differ, {first} and {second}", file=sys.stderr)
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    check_parser = commands.add_parser("check", help="audit built plugins and write their index entries")
    check_parser.add_argument("--build-dir", type=Path, required=True, help="the build tree, such as build/docker-release")
    check_parser.add_argument("--out", type=Path, required=True, help="where each plugin's index entry is written")
    check_parser.add_argument("--source-dir", type=Path, default=REPO_DIR, help="where plugins/<name>/ templates are read")
    check_parser.add_argument("plugins", nargs="+")

    reproduce_parser = commands.add_parser("reproduce", help="build twice from clean and compare the artifacts")
    reproduce_parser.add_argument("--ref", default="HEAD", help="the commit to build (default HEAD)")
    reproduce_parser.add_argument("plugins", nargs="+")

    argv = sys.argv[1:]
    build_options = []
    if "--" in argv:
        split = argv.index("--")
        argv, build_options = argv[:split], argv[split + 1 :]
    args = parser.parse_args(argv)

    if args.command == "check":
        return check(args)
    args.build_options = build_options
    return reproduce(args)


if __name__ == "__main__":
    sys.exit(main())

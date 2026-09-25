# Releasing plugins

## Pipeline

Plugins are released one at a time and published to channels in sets, the same model as
retrovert's playback plugins.

**A plugin tag**, `<plugin>/vX.Y.Z`, runs [`plugin-release.yml`](../.github/workflows/plugin-release.yml):

| Job | Runner | Does |
| --- | --- | --- |
| build | `ubuntu-24.04`, one per target | `./build.sh <plugin> release --docker --target <x86_64\|aarch64>`, then `scripts/release.py pack` |
| validate | `ubuntu-24.04`, `ubuntu-24.04-arm` | unpacks the archive, runs the publish gate and `smoke.toml` against what ships |
| store | `ubuntu-24.04` | attests the archives and the gate's index entry to this workflow and tag, and puts them on the `<plugin>/vX.Y.Z` release |

**A channel tag**, `<channel>/vN`, runs [`release.yml`](../.github/workflows/release.yml). It builds
nothing: it reads the versions `channels/<channel>/roster.toml` pins, downloads each pinned plugin
release, verifies its attestation came from `plugin-release.yml` at that plugin's tag, writes
`plugin-index.json` and `manifest.json`, stages the set on the `<channel>/vN` release, then
dispatches `channel-publish.yml` and waits for it. The signer's metadata write is the commit point.

A plugin whose pin did not change is carried into the new set byte for byte, so its digest is
the same and devices that have it fetch nothing. Each manifest entry names the plugin's version
and the commit its tag points at.

An archive carries `<plugin>.so`, its `.json5` template and, when the build produced one, `data/`
beside the `.so`. Archives are byte-reproducible: sorted entries, zero mtimes and owners, fixed
modes, `zstd -19 -T1`.

## Runbook

### Releasing a plugin

```console
$ git tag vamiga/v1.2.0 && git push origin vamiga/v1.2.0
```

A failed build, gate or smoke stores nothing. Fix it and tag the next version.

### Publishing to a channel

Pin the version in `channels/dev/roster.toml`, commit, then tag the channel:

```console
$ git tag dev/v3 && git push origin dev/v3
```

`N` must be above the channel's live version; the signer refuses anything else. Promoting to
`stable` is pinning the same versions in `channels/stable/roster.toml` and tagging `stable/vN`.
Taking a plugin off a channel is removing its pin.

### A publish fails part-way

Re-run the failed workflow. Assets already on a release must come out byte-identical, or staging
stops; missing ones are uploaded. If the signer run failed, read its log first: when it shows the
metadata commit, the generation is live and re-running fails at "Refuse to publish backwards",
which is harmless. Never delete a staged release or its assets.

### Checking a plugin locally

```console
$ ./build.sh vice_c64 release
$ scripts/release.py pack --build-dir build/release --target x86_64 --out packed vice_c64
$ mkdir -p stage/smoke && cp build/release/smoke/replay_smoke stage/smoke/
$ scripts/release.py unpack --artifacts packed --target x86_64 --into stage vice_c64
$ scripts/release.py validate --build-dir stage --out entries vice_c64
```

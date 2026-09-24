# Plugin channels

Trust anchors for Replay's plugin update channels and the workflows that sign
them. A channel is a TUF repository hosted on this repository's releases; the
frontend vendors `channels/<channel>/root.json` and verifies everything against
it with `retrovert-updater`.

| Channel | Base URL | Anchor | Status |
| --- | --- | --- | --- |
| `stable` | `https://github.com/FPGAArcade/replay_frontend_plugins/releases/download/stable/channel-metadata/` | `channels/stable/root.json` | live |
| `dev` | `https://github.com/FPGAArcade/replay_frontend_plugins/releases/download/dev/channel-metadata/` | `channels/dev/root.json` | live |

Devices follow `stable`. Developer desktops and the plugin canary follow `dev`.

| Release tag | Contents | Mutability |
| --- | --- | --- |
| `<channel>/channel-metadata` | TUF metadata and release-set manifests; its flat asset namespace is the base URL | `timestamp.json` replaced on every publish; everything else immutable |
| `<channel>/vN` | one release set: manifest, plugin index, plugin artifacts | immutable |

Generations are monotonic `vN`, latest-only, roll-forward-only.

## Creating a channel

With `retrovert-publish` built at the revision in [`publish.pin`](publish.pin):

```console
$ retrovert-publish init workspace
$ retrovert-publish check workspace
```

1. Commit `workspace/repository/metadata/root.json` as `channels/<channel>/root.json`.
2. Create the `channel-signing-<channel>` environment with deployment branches
   restricted to `main` and no required reviewers. Add the three secrets below
   from `workspace/keys/online/`.
3. Upload every file in `workspace/repository/metadata/` as a flat asset of a
   release tagged `<channel>/channel-metadata`.
4. Keep `workspace/keys/offline/root.pem` offline and safe. It never goes to
   CI. Whoever has it owns the channel.
5. Dispatch *Re-sign channels* with `channel=<channel>` and confirm the
   expiries move.
6. Vendor `root.json` into the frontend.

## Workflows

| Workflow | Trigger | Does |
| --- | --- | --- |
| [`channel-publish.yml`](../.github/workflows/channel-publish.yml) | dispatched by the gather with `channel`, `version`, `manifest_sha256` | pulls the live chain, refuses a version that is not newer, fetches the manifest by digest, publishes metadata with `timestamp.json` last, reads it back |
| [`channel-resign.yml`](../.github/workflows/channel-resign.yml) | 1st of each month 04:23 UTC, or dispatch | re-signs every online role of every committed channel, fails unless every expiry moved, reads it back |
| `ci.yml`, job *Channel anchors* | push, PR | parses every anchor, checks signatures meet threshold, warns 180 days before root expiry |

Both signing workflows build `retrovert-publish` from the full SHA in
`publish.pin` through [one composite action](../.github/actions/retrovert-publish/action.yml)
and share the concurrency group `sign-<channel>`.

A publish that fails part-way is recovered by publishing again, never by
deleting what landed. `stop_after=<n>` stops after `n` uploads, for drilling
that case.

## Secrets

Per channel, in `channel-signing-<channel>`:

| Secret | What it is |
| --- | --- |
| `CHANNEL_TARGETS_KEY` | `targets` private key, PKCS#8 PEM |
| `CHANNEL_SNAPSHOT_KEY` | `snapshot` private key, PKCS#8 PEM |
| `CHANNEL_TIMESTAMP_KEY` | `timestamp` private key, PKCS#8 PEM |

Releases are written with the workflow's built-in token; no personal access
token is needed.

An unset secret arrives as an empty string; the assemble step fails on that
before touching the channel.

## Expiry

| Role | Lifetime after signing | Renewed by |
| --- | --- | --- |
| `timestamp` | 90 days | `channel-resign.yml` |
| `snapshot`, `targets` | 1 year | `channel-resign.yml` |
| `root` | 10 years | the root key holder; not implemented in the signer yet |

A lapsed channel cannot self-recover: `pull` refuses expired metadata, so once
`timestamp` is 90 days old the re-sign fails and a publisher must re-sign from a
workspace of their own. If a root key is lost, the fix is a new root and a
frontend release carrying it.

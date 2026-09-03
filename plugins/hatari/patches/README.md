# Patches applied to `upstream/`

Empty. hatariB builds against the Replay SDK unmodified at the pinned revision, so the pin alone
reproduces the source state that builds.

`./build.sh hatari` applies every `*.patch` here in filename order after checking the submodule
out, so a change to hatariB is added as a numbered file (`0001-....patch`, from
`git format-patch`) rather than committed into `upstream/`.

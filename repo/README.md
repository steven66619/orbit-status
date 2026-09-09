# orbit-status package repository

Prebuilt packages and repository metadata for the **orbit suite** —
**orbit-status**, **orbiter** and **realspeed-cli** — deployed to GitHub
Pages at <https://steven66619.github.io/orbit-status/>. The GitHub Actions
workflow (`.github/workflows/publish-packages.yml`) builds every package in
`packages/` from source and regenerates the repository metadata on every
push that touches `packages/` or `repo/`.

## Arch Linux

Add to `/etc/pacman.conf`:

```ini
[orbit-status]
SigLevel = Optional
Server = https://steven66619.github.io/orbit-status/arch/x86_64
```

Then install with:

```sh
sudo pacman -Sy orbit-status orbiter realspeed-cli
```

`SigLevel = Optional` is required because packages are currently unsigned
(pacman's default is `Required`); signatures are verified automatically once
CI signing is configured.

## Release codenames

Codenames are reserved for **major releases** (like Ubuntu's "24.04 LTS
*Noble Numbat*"); minor/patch releases ship with a plain version number. The
first lineup keeps its codenames: 1.7 "Aurora", 1.8 "Nebula", 1.0.5 "Apollo",
1.1.0 "Columbia", 1.0.0 "Blitz".

Tag a major release with `v<version>-<codename>`:

```sh
git tag v1.8-nebula && git push origin v1.8-nebula
```

The `bump-versions` workflow picks up the highest-versioned tag, updates
`pkgver` and `_codename` in the PKGBUILD, and rebuilds the package. The
codename is visible in the package version (`orbit-status 1.8_nebula-1`);
pacman forbids hyphens in `pkgver`, so `_` is used instead.

### Releasing with `update.sh`

```sh
./update.sh pulsar                          # orbit-status 1.9 "Pulsar" (auto version)
./update.sh orbiter discovery               # orbiter 1.2.0 "Discovery"
./update.sh realspeed-cli 1.2.0 dash        # explicit version
./update.sh rename aurora                   # rename the latest codename on the fly
```

`update.sh` validates the codename, creates + pushes the
`v<version>-<codename>` tag, and triggers the bump workflow. `rename`
re-tags the latest release with a new codename (same version, same commit)
and rebuilds.

| Package | Theme | Releases | Next up |
|---|---|---|---|
| `orbit-status` | space/astronomy | 1.7 "Aurora", 1.8 "Nebula" | 1.9 "Pulsar", 1.10 "Quasar", "Supernova", "Eclipse", "Zenith", "Comet", "Cosmos", "Stellar", "Lunar", "Solar", "Galactic", "Celestial", "Astral", "Nova", "Meteor", "Gravity", "Horizon", "Orbit" |
| `orbiter` | space exploration | 1.0.5 "Apollo", 1.1.0 "Columbia" | 1.2.0 "Discovery", "Endeavor", "Enterprise", "Gemini", "Mercury", "Odyssey", "Pathfinder", "Pioneer", "Ranger", "Sputnik", "Voyager", "Challenger", "Atlantis" |
| `realspeed-cli` | speed | 1.0.0 "Blitz" | 1.1.0 "Dash", "Hyperdrive", "Lightspeed", "Ludicrous", "Mach", "Rocket", "Sprint", "Velocity", "Warp", "Zephyr", "Zoom", "Bolt", "Flash", "Jet", "Sonic", "Turbo" |

## Debian/Ubuntu

```sh
echo 'deb [signed-by=/etc/apt/keyrings/orbit-status.gpg] https://steven66619.github.io/orbit-status/apt stable main' | sudo tee /etc/apt/sources.list.d/orbit-status.list
sudo apt update
sudo apt install orbit-status
```

## Fedora/RHEL (yum/dnf)

```sh
sudo dnf install 'https://steven66619.github.io/orbit-status/yum/x86_64/repodata/repomd.xml'
# or add a repo file pointed at the same URL
```

## Void Linux

```sh
echo 'repository=https://steven66619.github.io/orbit-status/void/x86_64' >> /etc/xbps.d/10-orbit-status.conf
xbps-install -S orbit-status
```

## Regenerating metadata locally

```sh
./repo/update-repo.sh all   # or: apt | yum | arch | void
```

Requires the distro tools (`dpkg-scanpackages`, `createrepo_c`, `repo-add`,
`xbps-rindex`) and, for signatures, a GPG key matching the `GPG_KEY`
configured in the CI workflow.
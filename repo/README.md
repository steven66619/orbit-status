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
# orbit-status package repository

Prebuilt packages and repository metadata for **orbit-status**, deployed to
GitHub Pages at <https://steven66619.github.io/orbit-status/>. The GitHub
Actions workflow (`../.github/workflows/publish-packages.yml`) regenerates
and GPG-signs the repository metadata on every push that touches `repo/`,
so this directory only needs to contain the package files.

## Arch Linux

Add to `/etc/pacman.conf`:

```ini
[orbit-status]
SigLevel = Optional
Server = https://steven66619.github.io/orbit-status/arch/x86_64
```

Until GitHub Pages is enabled for the repository, the same repo is served
from the raw file host:

```ini
[orbit-status]
SigLevel = Optional
Server = https://raw.githubusercontent.com/steven66619/orbit-status/gh-pages/arch/x86_64
```

Then install with:

```sh
sudo pacman -Sy orbit-status
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
#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "Please run as root: sudo $0"
  exit 1
fi

dpkg --add-architecture arm64

cat >/etc/apt/sources.list.d/ubuntu-arm64.sources <<'EOF'
Types: deb
URIs: http://ports.ubuntu.com/ubuntu-ports/
Suites: noble noble-updates noble-backports
Components: main restricted universe multiverse
Architectures: arm64
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg

Types: deb
URIs: http://ports.ubuntu.com/ubuntu-ports/
Suites: noble-security
Components: main restricted universe multiverse
Architectures: arm64
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF

apt-get update
apt-get install -y \
  python3-dev:arm64 \
  libpython3-dev:arm64 \
  python3.12-dev:arm64 \
  libpython3.12-dev:arm64

echo "ARM64 Python development packages are installed."
echo "You can now run:"
echo "  ./scripts/build_linux_arm.sh cross Release"

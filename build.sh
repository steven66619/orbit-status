#!/bin/sh
set -e

INSTALL=1
for arg do
    case "$arg" in
        --no-install) INSTALL=0 ;;
        --help)
            echo "Usage: $0 [--no-install]"
            echo "  Builds orbit-status."
            echo "  Installs with sudo unless --no-install is given."
            exit 0 ;;
    esac
done

make clean 2>/dev/null || true
make

if [ "$INSTALL" = 1 ]; then
    sudo make install
else
    echo "Skipping install (--no-install)."
fi

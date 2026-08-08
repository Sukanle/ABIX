#!/usr/bin/env bash

set -e

echo "Installing Git Hooks..."

git config core.hooksPath .githooks

chmod +x .githooks/pre-commit

echo
echo "Git Hooks Installed Successfully."

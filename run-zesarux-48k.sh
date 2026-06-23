#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
exec make -C "$ROOT" run-zx48

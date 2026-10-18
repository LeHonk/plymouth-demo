#!/bin/sh
# Start a new demo as a fork of this base repo.
#
# usage: tools/new-demo.sh <demo-name> <target-dir>
#   e.g. tools/new-demo.sh fire ~/dev/demo-fire
#
# The base stays reachable as the "upstream" remote, so later core fixes
# come in with: git pull upstream main
# (the fork is set to merge on pull: rebasing would rewrite its own
# published commits, and git refuses to guess when both sides diverged)
set -eu

NAME=$1
DIR=$2
BASE=$(cd "$(dirname "$0")/.." && git rev-parse --show-toplevel)

case "$NAME" in
*[!a-z0-9-]* | "") echo "demo name: lowercase letters, digits and dashes only" >&2; exit 1 ;;
esac

git clone "$BASE" "$DIR"
cd "$DIR"
git remote rename origin upstream
git config pull.rebase false
sed -i -e "s/^DEMO_NAME *:=.*/DEMO_NAME   := $NAME/" \
       -e "s/^DEMO_TITLE *:=.*/DEMO_TITLE  := $NAME/" config.mk
git commit -q -am "Start demo '$NAME' from plymouth-demo base"

echo "Created $DIR. Next: edit demo/demo.c, then 'make preview' or 'make run'."
echo "Add your own remote with: git remote add origin <url>"

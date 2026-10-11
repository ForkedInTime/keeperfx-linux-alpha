#!/usr/bin/env bash
#
# Print the full sha of the KeeperFX team's commit this checkout last synced
# to: the newest Linux-only snapshot's source, or -- before the first snapshot
# -- the newest team commit merged directly. Needs the team's history fetched
# as the `upstream` remote for the fallback.
#
#   git log --oneline "$(packaging/linux-only/synced-upstream.sh)..upstream/master"
# lists what the team has done since.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
cd "$LINUX_ONLY_DIR/../.."

snap="$(last_snapshot HEAD)"
if [ -n "$snap" ]; then
    snapshot_trailer "$snap" "$SNAPSHOT_KEY"
else
    git merge-base HEAD "${1:-upstream/master}"
fi

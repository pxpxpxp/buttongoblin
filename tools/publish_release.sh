#!/bin/sh
# Publish a complete package; retry transient GitHub failures without hiding errors.
set -eu
[ "$#" -eq 4 ] || { echo 'Expected tag, bundle, title and notes file' >&2; exit 2; }
: "${DUAL_TAG:?}" "${GITHUB_SHA:?}"
tag=$1
bundle=$2
title=$3
notes=$4
latest=false
[ "$tag" != "$DUAL_TAG" ] || latest=true
publish_once() {
    if gh release view "$tag" >/dev/null 2>&1; then
        gh release upload "$tag" "$bundle.zip" "$bundle.zip.sha256" --clobber || return $?
        gh release edit "$tag" --title "$title" --notes-file "$notes" --latest="$latest"
    else
        gh release create "$tag" "$bundle.zip" "$bundle.zip.sha256" --target "$GITHUB_SHA" --title "$title" --notes-file "$notes" --latest="$latest"
    fi
}
attempt=1
until publish_once; do
    if [ "$attempt" -ge 3 ]; then
        echo "Release publishing failed after $attempt attempts: $tag" >&2
        exit 1
    fi
    echo "Release publishing attempt $attempt failed; retrying: $tag" >&2
    sleep "$((attempt * 5))"
    attempt=$((attempt + 1))
done

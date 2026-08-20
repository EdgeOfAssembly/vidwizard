#!/bin/sh
# Download a short Creative Commons clip with yt-dlp for optional net tests.
# Usage: scripts/fetch-test-clip.sh [output-dir]
set -eu

OUTDIR=${1:-testdata/net}
mkdir -p "$OUTDIR"

if ! command -v yt-dlp >/dev/null 2>&1; then
    echo "fetch-test-clip: yt-dlp not found" >&2
    exit 1
fi

# Big Buck Bunny trailer excerpt (short, widely mirrored). Cap duration.
URL=${VIDWIZARD_TEST_URL:-"https://www.youtube.com/watch?v=aqz-KE-bpKQ"}

yt-dlp -f "bv*[height<=360]+ba/b[height<=360]" \
    --download-sections "*0:00-0:03" \
    --no-playlist \
    -o "$OUTDIR/netclip.%(ext)s" \
    "$URL"

echo "fetch-test-clip: wrote files in $OUTDIR" >&2
ls -l "$OUTDIR"

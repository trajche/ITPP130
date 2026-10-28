#!/bin/sh
#
# Render the CUPS test page to raster with the ITPP130 PPD, run the filter on
# it, and check the TSPL it produces.
#
# Usage: test/run.sh path/to/rastertotspl path/to/munbyn-itpp130.ppd

set -eu

filter=$(realpath "$1")
ppd=$(realpath "$2")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

testprint=/usr/share/cups/data/testprint
[ -f "$testprint" ] || fail "missing $testprint (install cups)"

/usr/sbin/cupsfilter -p "$ppd" -m application/vnd.cups-raster \
  -o PageSize=w288h432 "$testprint" > "$work/page.ras" 2> "$work/cupsfilter.log" \
  || { cat "$work/cupsfilter.log" >&2; fail "cupsfilter could not render the test page"; }

PPD="$ppd" "$filter" 1 test test 1 "Darkness=10 zePrintRate=3 zeMediaTracking=Gap" \
  "$work/page.ras" > "$work/out.tspl" 2> "$work/filter.log" \
  || { cat "$work/filter.log" >&2; fail "filter exited non-zero"; }

# Header commands, in the order the printer expects them.
tr -d '\000' < "$work/out.tspl" | head -c 4096 | tr -d '\r' > "$work/header.txt"
for cmd in "SIZE 100 mm,150 mm" "REFERENCE 0,0" "DIRECTION 0,0" "GAP 3 mm,0 mm" \
           "DENSITY 10" "SPEED 3" "SETC AUTODOTTED OFF" "CLS" "BITMAP 0,0,100,"; do
  grep -qF "$cmd" "$work/header.txt" || fail "missing '$cmd' in TSPL header"
done

# BITMAP payload must be exactly width_bytes * height bytes.
bitmap=$(grep -aoE 'BITMAP 0,0,[0-9]+,[0-9]+,1,' "$work/header.txt" | head -n 1)
width_bytes=$(echo "$bitmap" | cut -d, -f3)
height=$(echo "$bitmap" | cut -d, -f4)
total=$(wc -c < "$work/out.tspl")
prefix=$(grep -abo 'BITMAP 0,0,' "$work/out.tspl" | head -n 1 | cut -d: -f1)
trailer=$(printf '\nPRINT 1,1\r\n' | wc -c)
payload=$((total - prefix - ${#bitmap} - trailer))
[ "$payload" -eq $((width_bytes * height)) ] \
  || fail "bitmap payload is $payload bytes, expected $((width_bytes * height))"

tail -c "$trailer" "$work/out.tspl" | od -c | grep -q 'P   R   I   N   T' \
  || fail "output does not end with PRINT 1,1"

# The test page has black content, so some bitmap bytes must be non-white.
tail -c +$((prefix + ${#bitmap} + 1)) "$work/out.tspl" | head -c "$payload" \
  | tr -d '\377' | wc -c | grep -qv '^ *0$' || fail "bitmap is entirely white"

echo "PASS: ${width_bytes}x${height} bitmap, $total bytes of TSPL"

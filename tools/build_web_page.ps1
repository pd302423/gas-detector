# Regenerates gas_detector_esp8266/web_page.h from dashboard/index.html.
#
# Edit the dashboard in dashboard/index.html — you can open that file directly
# in a browser and it runs against simulated data, so you can iterate on the
# interface without flashing anything. Then run this script to bake it into the
# firmware:
#
#     powershell -ExecutionPolicy Bypass -File tools\build_web_page.ps1
#
# The page is stored as a PROGMEM raw string literal so it lives in flash, not
# in the ESP8266's very limited RAM.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root "dashboard\index.html"
$dst  = Join-Path $root "gas_detector_esp8266\web_page.h"

$html = Get-Content $src -Raw -Encoding UTF8

# The raw-string delimiter must not appear anywhere in the page.
if ($html -match '\)PAGE"') {
  throw 'dashboard/index.html contains the raw-string delimiter — change the delimiter in this script.'
}

$header = @"
// web_page.h — GENERATED FILE, DO NOT EDIT BY HAND.
//
// Source:    dashboard/index.html
// Regenerate: powershell -ExecutionPolicy Bypass -File tools\build_web_page.ps1
//
// Stored in PROGMEM (flash), not RAM. Served by handleRoot() via send_P.

#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include <Arduino.h>

static const char PAGE_HTML[] PROGMEM = R"PAGE(
"@

$footer = @"
)PAGE";

#endif  // WEB_PAGE_H
"@

$out = $header + $html + $footer
Set-Content -Path $dst -Value $out -Encoding UTF8 -NoNewline

$kb = [math]::Round((Get-Item $dst).Length / 1KB, 1)
Write-Host "web_page.h written: $kb KB of flash."

# The Arduino IDE only compiles headers sitting beside the .ino, so each sketch
# folder needs its own copy of mq_curves.h. The root copy is the master; syncing
# it here means the copies cannot silently drift out of date.
$master = Join-Path $root "mq_curves.h"
foreach ($sketch in @("gas_detector_esp8266", "gas_detector_uno")) {
  Copy-Item $master (Join-Path $root "$sketch\mq_curves.h") -Force
}
Write-Host "mq_curves.h synced into both sketch folders."
if ($kb -gt 96) {
  Write-Host "Warning: the page is large. It still fits, but consider trimming if uploads get slow." -ForegroundColor Yellow
}

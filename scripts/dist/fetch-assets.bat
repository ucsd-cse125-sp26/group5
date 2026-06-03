@echo off
REM Downloads the full game asset bundle from the CSE 125 server and extracts it
REM next to this script, so assets\, maps\, and shaders\ land beside client.exe.
REM Ship this inside a binary-only release zip; run it once after unzipping.
REM Override the source by setting ASSET_BUNDLE_URL.
setlocal

set "DIR=%~dp0"
if not defined ASSET_BUNDLE_URL set "ASSET_BUNDLE_URL=https://cse125.ucsd.edu/2026/cse125g5/builds/asset-bundles/full-latest.zip"
set "ZIP=%DIR%full-latest.zip"

echo Downloading %ASSET_BUNDLE_URL%
curl -fL --retry 3 --retry-delay 2 -o "%ZIP%" "%ASSET_BUNDLE_URL%"
if errorlevel 1 (
  echo Download failed.
  exit /b 1
)

echo Extracting...
tar -xf "%ZIP%" -C "%DIR%"
if errorlevel 1 (
  echo tar failed, trying PowerShell Expand-Archive...
  powershell -NoProfile -Command "Expand-Archive -LiteralPath '%ZIP%' -DestinationPath '%DIR%' -Force"
  if errorlevel 1 (
    echo Extract failed.
    exit /b 1
  )
)

del "%ZIP%"
echo Done. assets\ maps\ shaders\ are next to client.exe.
endlocal

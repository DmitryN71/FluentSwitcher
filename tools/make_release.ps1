# Makes a FluentSwitcher release in build\release:
#   FluentSwitcher-<version>.zip        FluentSwitcher.exe, flags\, LICENSE, THIRD-PARTY-NOTICES.txt
#   FluentSwitcher-<version>-setup.exe  the installer (Inno Setup 6, installer\FluentSwitcher.iss)
# The version is the program's own (FLUENTSWITCHER_VERSION in CMakeLists.txt, read from the built exe).
#   powershell -File tools\make_release.ps1 [-NoBuild] [-Iscc <path to ISCC.exe>]
# WXDIR (a wxWidgets 3.3 source tree) is passed on to build.cmd, as for a normal build.
param(
    [switch]$NoBuild,
    [string]$Iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
if (-not $NoBuild) {
    & cmd /c "`"$root\build.cmd`""
    if ($LASTEXITCODE) { throw "build failed" }
}
$exe = "$root\build\x64-release\FluentSwitcher.exe"
$version = (Get-Item $exe).VersionInfo.ProductVersion
$out = "$root\build\release"
$folder = "$out\FluentSwitcher"
if (Test-Path $folder) { Remove-Item -LiteralPath $folder -Recurse -Force }
New-Item -ItemType Directory -Force $folder | Out-Null
Copy-Item $exe $folder
Copy-Item "$root\bin_files\flags" "$folder\flags" -Recurse
Copy-Item "$root\LICENSE", "$root\THIRD-PARTY-NOTICES.txt" $folder

$zip = "$out\FluentSwitcher-$version.zip"
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path $folder -DestinationPath $zip

if (-not (Test-Path $Iscc)) { throw "Inno Setup 6 is not found: $Iscc" }
& $Iscc /Q "/DAppVersion=$version" "/DSourceDir=$folder" "/DOutputDir=$out" "$root\installer\FluentSwitcher.iss"
if ($LASTEXITCODE) { throw "the installer failed" }
Get-Item $zip, "$out\FluentSwitcher-$version-setup.exe" | ForEach-Object { "{0}  {1:N0} bytes" -f $_.Name, $_.Length }

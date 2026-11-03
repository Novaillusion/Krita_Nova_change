<#
.SYNOPSIS
    Builds a Krita Android APK on Windows using Docker Desktop.

.EXAMPLE
    # from anywhere:
    powershell -ExecutionPolicy Bypass -File "H:\krita_windows_source\krita-master\packaging\android\local-build\build-android.ps1"

.EXAMPLE
    .\build-android.ps1 -NoPatches          # build without patches\*.patch

.EXAMPLE
    .\build-android.ps1 -Abi x86_64            # for the Android Studio emulator
    .\build-android.ps1 -Abi armeabi-v7a       # older 32-bit phones/tablets
    .\build-android.ps1 -Rebuild               # rebuild the Docker image
    .\build-android.ps1 -Shell                 # open a shell in the build container
#>
param(
    [ValidateSet('arm64-v8a', 'armeabi-v7a', 'x86_64')]
    [string]$Abi = 'arm64-v8a',

    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$BuildType = 'Release',

    # Defaults to the krita-master folder this script lives in
    [string]$Source = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path,

    # Where the APK + logs end up (next to krita-master by default)
    [string]$OutDir = (Join-Path (Split-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path -Parent) 'android-out'),

    [switch]$Rebuild,
    [switch]$Shell,
    # Build the source exactly as it is, without applying patches\*.patch
    [switch]$NoPatches
)

$ErrorActionPreference = 'Stop'
$Image  = 'krita-android-env'
$Volume = 'krita-android-work'   # persists deps, ccache and build dir between runs

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw "Docker not found. Install Docker Desktop (WSL2 backend) and start it first."
}
docker info *> $null
if ($LASTEXITCODE -ne 0) { throw "Docker Desktop is installed but not running. Start it and retry." }

if (-not (Test-Path (Join-Path $Source 'CMakeLists.txt'))) {
    throw "No Krita source found at $Source"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$imageExists = (docker images -q $Image) -ne $null -and (docker images -q $Image) -ne ''
if ($Rebuild -or -not $imageExists) {
    Write-Host "==> Building Docker image $Image (first time: ~10 min, ~8 GB download)" -ForegroundColor Cyan
    docker build -t $Image $PSScriptRoot
    if ($LASTEXITCODE -ne 0) { throw "docker build failed" }
}

docker volume create $Volume | Out-Null

$runArgs = @(
    'run', '--rm', '-it',
    '-v', "${Source}:/src:ro",
    '-v', "${Volume}:/work",
    '-v', "${OutDir}:/out",
    '-e', "KRITA_ANDROID_ABI=$Abi",
    '-e', "KRITA_BUILD_TYPE=$BuildType",
    '-e', "KRITA_SKIP_PATCHES=$([int][bool]$NoPatches)"
)

if ($Shell) {
    docker @runArgs --entrypoint bash $Image
    exit $LASTEXITCODE
}

Write-Host "==> Building Krita for Android ($Abi, $BuildType)" -ForegroundColor Cyan
Write-Host "    source : $Source"
Write-Host "    output : $OutDir"
$patches = Get-ChildItem (Join-Path $PSScriptRoot 'patches') -Filter *.patch -ErrorAction SilentlyContinue | Sort-Object Name
if ($NoPatches) {
    Write-Host "    patches: (disabled)"
} elseif ($patches) {
    Write-Host "    patches: $(($patches | ForEach-Object Name) -join ', ')"
}
# Run the build script from the mounted source, so edits to it (and new patches)
# take effect without rebuilding the Docker image.
docker @runArgs --entrypoint bash $Image /src/packaging/android/local-build/build-krita-android.sh
if ($LASTEXITCODE -ne 0) { throw "Build failed - see logs in $OutDir" }

Write-Host ""
Write-Host "==> Done. APK(s):" -ForegroundColor Green
Get-ChildItem $OutDir -Filter *.apk | ForEach-Object { "    " + $_.FullName }
Write-Host ""
Write-Host "Install with:  adb install -r -d `"<apk path>`""

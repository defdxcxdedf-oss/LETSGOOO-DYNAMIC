param([string]$BuildDirectory = "build-windows")
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Set-Location (Split-Path $PSScriptRoot -Parent)
function Run([scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE" }
}
if (-not $env:GEODE_SDK) { throw "Set GEODE_SDK to the Geode 5.11.2 SDK (with installed binaries and CLI)." }
if ((Get-Content (Join-Path $env:GEODE_SDK "VERSION")).Trim() -ne "5.11.2") { throw "Geode 5.11.2 is required." }
$bindings = Join-Path $BuildDirectory "bindings-pinned"
$bindingsRevision = "6f163558a5e2ac47b9121c6721fd32322c8e188b"
if (-not (Test-Path $bindings)) {
    New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
    Run { git init $bindings }
    Run { git -C $bindings remote add origin https://github.com/geode-sdk/bindings.git }
}
$actualRevision = [string](& git -C $bindings rev-parse --verify --quiet HEAD 2>$null)
if ($LASTEXITCODE -ne 0 -or $actualRevision.Trim() -ne $bindingsRevision) {
    Run { git -C $bindings fetch --depth 1 origin $bindingsRevision }
    Run { git -C $bindings checkout --detach $bindingsRevision }
}
$actualRevision = [string](& git -C $bindings rev-parse --verify HEAD)
if ($LASTEXITCODE -ne 0 -or $actualRevision.Trim() -ne $bindingsRevision) { throw "Unexpected bindings revision." }
$bindingsPath = (Resolve-Path $bindings).Path
$env:GEODE_BINDINGS_REPO_PATH = $bindingsPath
Run { cmake -S . -B $BuildDirectory -A x64 -DIAX_BUILD_MOD=ON "-DGEODE_BINDINGS_REPO_PATH=$bindingsPath" }
Run { cmake --build $BuildDirectory --config Release --parallel 2 }
Run { ctest --test-dir $BuildDirectory -C Release --output-on-failure }
$env:GDGEN_SIM_LIB = (Resolve-Path "$BuildDirectory/Release/gdsim.dll").Path
$env:IAX_GATE_TEST = (Resolve-Path "$BuildDirectory/Release/iax_gate_tests.exe").Path
Run { py -3.12 -m unittest discover -s tests -v }
$runtime = Join-Path $BuildDirectory "python-3.12.10-embed-amd64.zip"
$runtimeHash = "4ACBED6DD1C744B0376E3B1CF57CE906F9DC9E95E68824584C8099A63025A3C3"
if ((Test-Path $runtime) -and (Get-FileHash $runtime -Algorithm SHA256).Hash -ne $runtimeHash) { Remove-Item $runtime }
if (-not (Test-Path $runtime)) {
    Invoke-WebRequest "https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip" -OutFile "$runtime.partial" -TimeoutSec 120
    if ((Get-FileHash "$runtime.partial" -Algorithm SHA256).Hash -ne $runtimeHash) { throw "Untrusted runtime download" }
    Move-Item "$runtime.partial" $runtime -Force
}
$package = Join-Path $BuildDirectory "InputAlignExternal-windows.zip"
Run { py -3.12 scripts/package-windows.py --build $BuildDirectory --python-runtime $runtime --output $package --native $env:GDGEN_SIM_LIB }
Write-Host "Package ready: $package"

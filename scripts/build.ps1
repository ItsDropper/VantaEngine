param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [switch]$Test,
    [switch]$Run,
    [switch]$Clean,

    [ValidateSet("D3D12", "D3D11")]
    [string]$Backend = "D3D12"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build"

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe was not found. Install Visual Studio with the MSVC x64 C++ tools."
    }

    $path = & $vswhere -latest -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $path -or -not (Test-Path $path)) {
        throw "No Visual Studio installation with the MSVC x64 toolchain was found."
    }

    return $path.Trim()
}

$vsPath = Find-VisualStudio
$devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
$cmakeBundled = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctestBundled = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
$ninjaBundled = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

$cmake = if (Test-Path $cmakeBundled) { $cmakeBundled } else {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) { $command.Source } else { $null }
}
$ctest = if (Test-Path $ctestBundled) { $ctestBundled } else {
    $command = Get-Command ctest -ErrorAction SilentlyContinue
    if ($command) { $command.Source } else { $null }
}
$ninja = if (Test-Path $ninjaBundled) { $ninjaBundled } else {
    $command = Get-Command ninja -ErrorAction SilentlyContinue
    if ($command) { $command.Source } else { $null }
}

if (-not (Test-Path $devCmd)) { throw "Visual Studio developer environment script not found: $devCmd" }
if (-not $cmake) { throw "CMake was not found. Add the Visual Studio CMake component or install CMake." }
if (-not $ctest) { throw "CTest was not found. Add the Visual Studio CMake component or install CMake." }
if (-not $ninja) { throw "Ninja was not found. Add the Visual Studio CMake/Ninja components or install Ninja." }

if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$configure = 'call "{0}" -arch=x64 -host_arch=x64 >nul && "{1}" -S "{2}" -B "{3}" -G Ninja -DCMAKE_BUILD_TYPE={4} -DCMAKE_MAKE_PROGRAM="{5}"' -f $devCmd, $cmake, $root, $buildDir, $Configuration, $ninja
Write-Host "Configuring VantaEngine ($Configuration)..."
& $env:ComSpec /d /s /c $configure
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE." }

$build = 'call "{0}" -arch=x64 -host_arch=x64 >nul && "{1}" --build "{2}"' -f $devCmd, $cmake, $buildDir
Write-Host "Building VantaEngine..."
& $env:ComSpec /d /s /c $build
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }

$exe = Join-Path $buildDir "bin\VantaEngine.exe"

if ($Test) {
    $testCommand = 'call "{0}" -arch=x64 -host_arch=x64 >nul && "{1}" --test-dir "{2}" --output-on-failure' -f $devCmd, $ctest, $buildDir
    Write-Host "Running tests..."
    & $env:ComSpec /d /s /c $testCommand
    if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE." }
}

if ($Run) {
    if (-not (Test-Path $exe)) { throw "Expected executable was not created: $exe" }
    Write-Host "Launching VantaEngine with the $Backend renderer..."
    & $exe "--backend=$($Backend.ToLowerInvariant())"
    if ($LASTEXITCODE -ne 0) { throw "Engine exited with code $LASTEXITCODE." }
}

Write-Host "Done. Executable: $exe"

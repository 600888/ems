# 在对应工具链终端中构建；依赖须使用相同架构和编译器。
[CmdletBinding()]
param(
    [ValidateSet('msvc', 'mingw')]
    [string]$Toolchain = 'msvc',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [string]$PrefixPath = '',
    [string[]]$CMakeArgs = @(),
    [ValidateRange(1, 128)]
    [int]$Jobs = 2,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
$backendDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'backend'
$preset = "windows-$Toolchain"
$buildDir = Join-Path $backendDir "build/$preset"

Push-Location $backendDir
try {
    $configureArgs = @('--preset', $preset)
    if ($PrefixPath) {
        $configureArgs += "-DCMAKE_PREFIX_PATH=$PrefixPath"
    }
    if ($Toolchain -eq 'mingw') {
        $configureArgs += "-DCMAKE_BUILD_TYPE=$Configuration"
    }
    if ($Test) {
        $configureArgs += '-DBUILD_TESTS=ON'
    }

    & cmake @configureArgs @CMakeArgs
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

    & cmake --build --preset $preset --config $Configuration --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }

    if ($Test) {
        & ctest --test-dir $buildDir -C $Configuration --output-on-failure --no-tests=error
        if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
    }
} finally {
    Pop-Location
}

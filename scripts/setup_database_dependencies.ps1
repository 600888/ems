[CmdletBinding()]
param(
    [string]$BuildDirectory = '',
    [ValidateSet('msvc', 'mingw')]
    [string]$Toolchain = 'msvc',
    [ValidateRange(1, 32)]
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $projectRoot 'backend/build/database-deps'
}
$dependencyRoot = [IO.Path]::GetFullPath($BuildDirectory)
$build = Join-Path $dependencyRoot "mariadb-$Toolchain"
$prefix = Join-Path $build 'install'
$package = 'mariadb-connector-c-3.4.8-src'
$archive = Join-Path $dependencyRoot "$package.tar.gz"
$source = Join-Path $dependencyRoot $package
New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null

if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri "https://archive.mariadb.org/connector-c-3.4.8/$package.tar.gz" `
        -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne
    '156aed3b49f857d0ac74fb76f1982968bcbfd8382da3f5b6ae71f616729920d7') {
    throw 'MariaDB Connector/C checksum mismatch.'
}
if (-not (Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))) {
    & tar -xzf $archive -C $dependencyRoot
    if ($LASTEXITCODE -ne 0) { throw 'MariaDB Connector/C extraction failed.' }
}

$compilerArgs = if ($Toolchain -eq 'msvc') {
    @('-DCMAKE_C_COMPILER=cl')
} else {
    @('-DCMAKE_C_COMPILER=gcc')
}
# 使用系统 TLS 和源码内的 zlib，客户端依赖不混用其他工具链的库。
$configureArgs = @(
    '-S', $source, '-B', $build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_INSTALL_PREFIX=$prefix", '-DWITH_SSL=SCHANNEL', '-DWITH_EXTERNAL_ZLIB=OFF',
    '-UZLIB_INCLUDE_DIR', '-UZLIB_LIBRARY', '-DWITH_UNIT_TESTS=OFF', '-DWITH_CURL=OFF',
    '-DWITH_MSI=OFF'
) + $compilerArgs
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'MariaDB Connector/C configure failed.' }

& cmake --build $build --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'MariaDB Connector/C build failed.' }
& cmake --install $build
if ($LASTEXITCODE -ne 0) { throw 'MariaDB Connector/C install failed.' }

Write-Output "Database dependencies installed: $prefix"
Write-Output "Configure next: cmake --preset windows-$Toolchain -S backend"

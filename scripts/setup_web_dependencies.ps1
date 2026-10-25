[CmdletBinding()]
param(
    [string]$BuildDirectory = '',
    [ValidateSet('msvc', 'mingw')]
    [string]$Toolchain = 'msvc',
    [ValidateSet('none', 'openssl')]
    [string]$TlsProvider = 'none',
    [ValidateRange(1, 32)]
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
& chcp.com 65001 | Out-Null
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $projectRoot "backend/build/web-deps-$Toolchain"
}
$dependencyRoot = [IO.Path]::GetFullPath($BuildDirectory)
$prefix = Join-Path $dependencyRoot 'install'
New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
$compilerArgs = if ($Toolchain -eq 'msvc') {
    @('-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl')
} else {
    @('-DCMAKE_C_COMPILER=gcc', '-DCMAKE_CXX_COMPILER=g++')
}

# 只补充框架依赖，Drogon 始终使用用户放入 backend/third_party/drogon 的源码。
$trantorRevision = '78e480f5e138acdc05f76c55a2716718cd846364'
$checksums = @{
    'jsoncpp-1.9.6' = 'f93b6dd7ce796b13d02c108bc9f79812245a82e577581c4c9aabe57075c90ea2'
    'zlib-1.3.1' = '9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23'
    "trantor-$trantorRevision" = '60487a296530a330d753fe3c5e492a885d043a5b8d7ffa4d9bdc96b2813a394b'
}
$packages = @(
    @{ Name='jsoncpp-1.9.6'; Archive='jsoncpp.tar.gz'; Url='https://codeload.github.com/open-source-parsers/jsoncpp/tar.gz/refs/tags/1.9.6'; Options=@('-DJSONCPP_WITH_TESTS=OFF','-DJSONCPP_WITH_POST_BUILD_UNITTEST=OFF','-DBUILD_SHARED_LIBS=OFF','-DBUILD_OBJECT_LIBS=OFF') },
    @{ Name='zlib-1.3.1'; Archive='zlib.tar.gz'; Url='https://zlib.net/fossils/zlib-1.3.1.tar.gz'; Options=@('-DZLIB_BUILD_EXAMPLES=OFF') },
    @{ Name="trantor-$trantorRevision"; Archive="trantor-$trantorRevision.tar.gz"; Url="https://codeload.github.com/an-tao/trantor/tar.gz/$trantorRevision"; Options=@('-DBUILD_TESTING=OFF','-DBUILD_SHARED_LIBS=OFF',"-DTRANTOR_USE_TLS=$TlsProvider",'-DTRANTOR_USE_CARES=OFF') }
)

foreach ($package in $packages) {
    $archive = Join-Path $dependencyRoot $package.Archive
    if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $package.Url -OutFile $archive }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $checksums[$package.Name]) {
        throw "Dependency checksum mismatch: $($package.Name)"
    }

    $source = Join-Path $dependencyRoot $package.Name
    if (-not (Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))) {
        & tar -xzf $archive -C $dependencyRoot
        if ($LASTEXITCODE -ne 0) { throw "Extract failed: $($package.Name)" }
    }

    $build = Join-Path $dependencyRoot "$($package.Name)-build"
    & cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 `
        -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded `
        "-DCMAKE_INSTALL_PREFIX=$prefix" "-DCMAKE_PREFIX_PATH=$prefix" @compilerArgs @($package.Options)
    if ($LASTEXITCODE -ne 0) { throw "Configure failed: $($package.Name)" }

    & cmake --build $build --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $($package.Name)" }
    & cmake --install $build
    if ($LASTEXITCODE -ne 0) { throw "Install failed: $($package.Name)" }
}

Write-Output "Web dependencies installed: $prefix"
Write-Output "Configure next: cmake --preset windows-$Toolchain -S backend"

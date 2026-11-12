[CmdletBinding()]
param(
    [string]$BuildDirectory = ''
)

$ErrorActionPreference = 'Stop'
$Version = '3.4.3.9'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $projectRoot 'backend/build/tdengine-deps'
}
$dependencyRoot = [IO.Path]::GetFullPath($BuildDirectory)
New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'backend/tmp') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'backend/log/taos') -Force | Out-Null
$packageName = "tdengine-tsdb-oss-client-$Version-windows-x64.exe"
$package = Join-Path $dependencyRoot $packageName
function Download-File([string]$Url, [string]$Destination) {
    # Python 使用项目已有的 OpenSSL HTTPS 客户端，兼容官方站点与 Windows Schannel 的差异。
    & python -c 'import pathlib,sys,urllib.request; pathlib.Path(sys.argv[2]).write_bytes(urllib.request.urlopen(sys.argv[1], timeout=60).read())' $Url $Destination
    if ($LASTEXITCODE -ne 0) { throw 'Dependency download failed.' }
}
# 官方 3.4.3.9 包；首次核验其官方目录 MD5 后固定 SHA-256，避免下载清单漂移。
$uri = [Uri]"https://downloads.taosdata.com/tdengine-tsdb-oss/$Version/$packageName"
if (-not (Test-Path -LiteralPath $package)) {
    Download-File $uri.AbsoluteUri $package
}
if ((Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash -ne
    'E9D5F0BAC1863C1356AA8B3AF674AABD53E05C6502F661EDD8B1441DDAB9D0D2') {
    throw 'TDengine package checksum mismatch.'
}
$extractorZip = Join-Path $dependencyRoot 'innoextract-1.9-windows.zip'
$extractorRoot = Join-Path $dependencyRoot 'innoextract'
if (-not (Test-Path -LiteralPath (Join-Path $extractorRoot 'innoextract.exe'))) {
    if (-not (Test-Path -LiteralPath $extractorZip)) {
        Download-File 'https://github.com/dscharrer/innoextract/releases/download/1.9/innoextract-1.9-windows.zip' $extractorZip
    }
    if ((Get-FileHash -LiteralPath $extractorZip -Algorithm SHA256).Hash -ne
        '6989342C9B026A00A72A38F23B62A8E6A22CC5DE69805CF47D68AC2FEC993065') {
        throw 'Extractor checksum mismatch.'
    }
    Expand-Archive -LiteralPath $extractorZip -DestinationPath $extractorRoot -Force
}
$sdkRoot = Join-Path $dependencyRoot 'sdk'
# 仅提取 app 文件；不执行安装器，不写 System32、注册表或系统环境变量。
& (Join-Path $extractorRoot 'innoextract.exe') --extract --silent --output-dir $sdkRoot --include app $package
if ($LASTEXITCODE -ne 0) { throw 'TDengine client extraction failed.' }
$include = (Join-Path $sdkRoot 'app/include').Replace('\', '/')
$library = (Join-Path $sdkRoot 'app/driver/taos.lib').Replace('\', '/')
if (-not (Test-Path -LiteralPath "$include/taos.h") -or -not (Test-Path -LiteralPath $library)) {
    throw 'TDengine SDK payload is incomplete.'
}
Write-Output "TDengine $Version SDK extracted into $sdkRoot"
Write-Output "Configure: cmake -S backend -B backend/build/windows-msvc -DEMS_DATABASE_TAOS_INCLUDE_DIR=$include -DEMS_DATABASE_TAOS_LIBRARY=$library"

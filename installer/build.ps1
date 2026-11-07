# SPDX-License-Identifier: AGPL-3.0-only
#
# インストーラー（Burn バンドル）と更新パッケージを作る。CI と手元で同じ手順を使う。
# 先に CMake でビルドしておくこと（cmake --build build --config Release）。
#
#   pwsh installer/build.ps1 [-BuildDir build] [-Config Release] [-OutDir out/installer]
#
# 必要なもの: WiX Toolset 5.0.2（dotnet tool install --global wix --version 5.0.2）と、
# 拡張 WixToolset.Util.wixext/5.0.2、WixToolset.Bal.wixext/5.0.2（wix extension add -g ...）。
#
# 出力:
#   VOCROZ-Setup-<版>.exe   配布するインストーラー
#   vocroz-app-<版>.zip     pa-updater 用の更新パッケージ（app-<版>\ の中身）
#   vocroz-app-<版>.zip.sha256
param(
    [string]$BuildDir = 'build',
    [string]$Config = 'Release',
    [string]$OutDir = 'out/installer'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'signing.ps1')

# WebView2 Evergreen Bootstrapper の固定のダウンロード先（Microsoft の配布ガイドの URL）。
# Bootstrapper は小さく、実行時に最新のランタイムを取得するので、版を固定する必要がない。
$WebView2BootstrapperUrl = 'https://go.microsoft.com/fwlink/p/?LinkId=2124703'

function Invoke-Checked {
    param([Parameter(Mandatory)][string]$Command, [string[]]$Arguments)
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command が失敗しました（終了コード $LASTEXITCODE）"
    }
}

function Get-ProjectVersion {
    # バージョンの正本はルートの CMakeLists.txt の project()。ビルド済みのキャッシュから読む。
    $cache = Get-Content (Join-Path $BuildDir 'CMakeCache.txt')
    $line = $cache | Where-Object { $_ -match '^CMAKE_PROJECT_VERSION:STATIC=(.+)$' } | Select-Object -First 1
    if (-not $line) {
        throw "$BuildDir/CMakeCache.txt からバージョンを読めません。先に CMake で構成してください。"
    }
    return ($line -replace '^CMAKE_PROJECT_VERSION:STATIC=', '')
}

$version = Get-ProjectVersion
$out = New-Item -ItemType Directory -Force -Path $OutDir
$stage = Join-Path $out 'stage'
$redist = Join-Path $out 'redist'
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $redist | Out-Null

# 1. CMake の install でレイアウトを作る（起動スタブはルート、それ以外は app\）。
Invoke-Checked cmake @('--install', $BuildDir, '--config', $Config, '--prefix', $stage)

# 2. 自分たちの実行ファイルに署名する（証明書がなければスキップ）。
$ownExecutables = @(Join-Path $stage 'vocroz.exe') + (Get-ChildItem (Join-Path $stage 'app') -Filter '*.exe').FullName
Invoke-VocrozSign -Path $ownExecutables

# 3. 更新パッケージ。署名した後の app\ をそのまま ZIP にする。
$package = Join-Path $out "vocroz-app-$version.zip"
Remove-Item -Force $package -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stage 'app\*') -DestinationPath $package
$hash = (Get-FileHash -Algorithm SHA256 $package).Hash.ToLowerInvariant()
Set-Content -NoNewline -Path "$package.sha256" -Value $hash

# 4. WebView2 の Bootstrapper を取得する。
$bootstrapper = Join-Path $redist 'MicrosoftEdgeWebview2Setup.exe'
if (-not (Test-Path $bootstrapper)) {
    Invoke-WebRequest -Uri $WebView2BootstrapperUrl -OutFile $bootstrapper
}

# 5. MSI を作って署名する。
$msi = Join-Path $out 'vocroz.msi'
Invoke-Checked wix @(
    'build', (Join-Path $PSScriptRoot 'Package.wxs'),
    '-arch', 'x64',
    '-ext', 'WixToolset.Util.wixext',
    '-d', "VocrozVersion=$version",
    '-bindpath', "root=$stage",
    '-bindpath', "app=$(Join-Path $stage 'app')",
    '-o', $msi)
Invoke-VocrozSign -Path @($msi)

# 6. バンドルを作る。署名するときは、Burn エンジンを取り出して署名してから戻し、
#    バンドル全体にも署名する（エンジンの署名がないと、実行時に昇格やキャッシュで失敗するため）。
$bundle = Join-Path $out "VOCROZ-Setup-$version.exe"
Invoke-Checked wix @(
    'build', (Join-Path $PSScriptRoot 'Bundle.wxs'),
    '-arch', 'x64',
    '-ext', 'WixToolset.Bal.wixext',
    '-ext', 'WixToolset.Util.wixext',
    '-d', "VocrozVersion=$version",
    '-bindpath', "msi=$out",
    '-bindpath', "redist=$redist",
    '-o', $bundle)
if (Test-VocrozSigningEnabled) {
    $engine = Join-Path $out 'engine.exe'
    Invoke-Checked wix @('burn', 'detach', $bundle, '-engine', $engine)
    Invoke-VocrozSign -Path @($engine)
    Invoke-Checked wix @('burn', 'reattach', $bundle, '-engine', $engine, '-o', $bundle)
    Invoke-VocrozSign -Path @($bundle)
    Remove-Item -Force $engine
}

Write-Host "インストーラー: $bundle"
Write-Host "更新パッケージ: $package (sha256 $hash)"

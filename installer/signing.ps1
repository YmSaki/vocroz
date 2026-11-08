# SPDX-License-Identifier: AGPL-3.0-only
#
# コード署名（Authenticode）の 1 か所の差し込み口。build.ps1 から dot-source して使う。
# 署名の名義が決まるまでは、証明書を設定せずに未署名のまま配布の仕組みを通す
# （docs/adr/0013-installer-and-updater.md）。証明書が届いたら、次の環境変数を設定するだけで
# 実行ファイル、MSI、バンドル（Burn エンジンを含む）に署名する。
#
#   VOCROZ_SIGN_CERT_THUMBPRINT  証明書ストアにある署名用証明書の拇印（SHA-1）。空なら署名しない
#   VOCROZ_SIGN_TIMESTAMP_URL    RFC 3161 のタイムスタンプサーバー（省略時は DigiCert）
#   VOCROZ_SIGNTOOL              signtool.exe のパス（省略時は PATH から探す）

function Test-VocrozSigningEnabled {
    return -not [string]::IsNullOrWhiteSpace($env:VOCROZ_SIGN_CERT_THUMBPRINT)
}

function Invoke-VocrozSign {
    param([Parameter(Mandatory)][string[]]$Path)

    if (-not (Test-VocrozSigningEnabled)) {
        Write-Host "署名をスキップします（VOCROZ_SIGN_CERT_THUMBPRINT が未設定）: $($Path -join ', ')"
        return
    }
    $signtool = if ($env:VOCROZ_SIGNTOOL) { $env:VOCROZ_SIGNTOOL } else { 'signtool.exe' }
    $timestamp = if ($env:VOCROZ_SIGN_TIMESTAMP_URL) { $env:VOCROZ_SIGN_TIMESTAMP_URL } else { 'http://timestamp.digicert.com' }
    # タイムスタンプを付け、証明書の期限が切れた後も署名を検証できるようにする。
    & $signtool sign /sha1 $env:VOCROZ_SIGN_CERT_THUMBPRINT /fd SHA256 /tr $timestamp /td SHA256 @Path
    if ($LASTEXITCODE -ne 0) {
        throw "署名に失敗しました: $($Path -join ', ')"
    }
}

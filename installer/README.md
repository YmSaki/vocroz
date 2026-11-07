# installer

VOCROZ のインストーラー（WiX Toolset 5.0.2 の Burn バンドルと MSI）の定義です。
構成と判断の理由は [docs/adr/0009-installer-and-updater.md](../docs/adr/0009-installer-and-updater.md) にあります。

| ファイル | 内容 |
| --- | --- |
| `Bundle.wxs` | 配布する exe。WebView2 Runtime の導入と MSI の実行、アンインストールの選択 |
| `Package.wxs` | ユーザー単位の MSI。起動スタブ、`app-<版>\`、ショートカット、`vocroz://` |
| `theme/` | バンドルの画面（WixStdBA のテーマと日本語の文字列） |
| `build.ps1` | インストーラーと更新パッケージを作る。CI と手元で同じ手順 |
| `signing.ps1` | コード署名の差し込み口。証明書が未設定なら署名をスキップする |

## 作り方（Windows）

```powershell
dotnet tool install --global wix --version 5.0.2
wix extension add -g WixToolset.Util.wixext/5.0.2 WixToolset.Bal.wixext/5.0.2

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
pwsh installer/build.ps1
```

`out/installer/` に `VOCROZ-Setup-<版>.exe` と更新パッケージ `vocroz-app-<版>.zip` ができます。

## 署名

署名の名義が決まるまでは未署名で作ります。証明書が届いたら、`signing.ps1` の先頭に書いた環境変数
（`VOCROZ_SIGN_CERT_THUMBPRINT` など）を設定するだけで、実行ファイル、MSI、Burn エンジン、バンドルの順に署名します。
CI では Secrets から同じ環境変数を渡します。

更新マニフェストは Authenticode とは別に Ed25519 で署名します。鍵の生成と署名は `tools/release/` を使い、
公開鍵は CMake の `VOCROZ_UPDATE_PUBLIC_KEY` に渡してビルドします。

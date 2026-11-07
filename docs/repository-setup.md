# リポジトリの初期設定

リポジトリの管理者が最初に一度だけ行う設定です。開発の進め方そのものは [AGENTS.md](../AGENTS.md) の第 16 節と第 17 節にあります。

1. **Actions の「Repository setup」を実行する**: ラベルとマイルストーンが作られます。
2. **@claude を使う場合**: Settings → Secrets and variables → Actions に `CLAUDE_CODE_OAUTH_TOKEN`（Claude の契約を使う場合。手元で `claude setup-token` を実行して発行）か `ANTHROPIC_API_KEY` を登録し、[Claude GitHub App](https://github.com/apps/claude) をこのリポジトリにインストールします。
3. **main を保護する（推奨）**: Settings → Branches でルールを追加し、「PR を必須にする」と「CI の成功を必須にする」を有効にします。

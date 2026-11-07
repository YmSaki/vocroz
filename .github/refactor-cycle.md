<!-- このテンプレートは .github/workflows/refactor-cycle.yml が毎月使う。手順の正本は AGENTS.md 第 16 節。 -->

AGENTS.md 第 15 節「配置の地図」とコードベース全体を照合し、ずれを直すサイクルです。

## 手順

- [ ] 前回のサイクル以降に入った PR を確認する
- [ ] 機械的な検査を回し、結果をこの Issue に貼る
    - `git ls-files '*.cpp' '*.h' | xargs clang-format -i`
    - `run-clang-tidy -p build -quiet '(apps|modules)/.*/src/'`
    - `cd infra/rendezvous && gofmt -l . && go vet ./...`
- [ ] 地図と照合し、ずれを下の「ずれ一覧」に書き出す（形式は AGENTS.md 第 16.3 節）
- [ ] ずれを「1 PR = 1 種類の移動」にまとめて PR を出す（動作は変えない）
- [ ] 地図そのものに穴や矛盾が見つかったら、先に AGENTS.md を直す PR を出す

Claude に任せる場合は、この Issue に `@claude AGENTS.md 第 16 節の手順で照合して、ずれ一覧を作って` とコメントする。

## ずれ一覧

| # | 現在地 | あるべき位置と形 | 根拠 | 直し方 | PR |
| --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |

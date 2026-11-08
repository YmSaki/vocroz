# メッセージの JSON の例

UI とエンジン間の全メッセージの JSON の例です（`docs/adr/0014-control-protocol-messages.md`）。
C++ の型（`modules/control-protocol`）と UI の型（`ui/src/ipc/messages.ts`）が同じ形を表していることを、両方のテストがこの例を読んで確かめます。

- `ui_to_engine.json`: UI からエンジンへのメッセージ
- `engine_to_ui.json`: エンジンから UI へのメッセージ

すべての種別と、状態とエラーコードのすべての正規名を、少なくとも 1 回ずつ含めます。
メッセージを追加、変更したら、この例も同じ PR で更新します。

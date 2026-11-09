// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine のエントリポイント。親機のオーディオエンジンで、remote-pa（親機モード）が起動する
// 内部プロセス。引数のパース → コンポジションルートの構築 → 実行。
//   --version          : バージョンを表示して終了
//   --data-dir=<パス>  : 設定、ログ、クラッシュダンプの置き場所。パイプの名前もここから決まる
//   --log-level=<段階> : error、warn、info、debug

#include <iostream>
#include <variant>

#include "vocroz/common/cli.h"
#include "vocroz/common/version_info.h"

#include "engine_constants.h"
#include "launch_options.h"

#if defined(_WIN32)
#include "composition_root.h"
#endif

int main(int argc, char** argv) {
    namespace engine = vocroz::engine;
    const auto parsed = engine::parse_launch_options(vocroz::common::make_args(argc, argv));
    if (const auto* error = std::get_if<engine::LaunchError>(&parsed)) {
        std::cerr << "pa-engine: " << error->message << '\n';
        return engine::kExitUsage;
    }
    const auto& options = std::get<engine::LaunchOptions>(parsed);
    if (options.command == engine::Command::kVersion) {
        std::cout << vocroz::common::version_line("pa-engine") << '\n';
        return engine::kExitOk;
    }

#if defined(_WIN32)
    engine::CompositionRoot root(options);
    return root.run();
#else
    // 名前付きパイプと WASAPI は Windows にしかないので、ほかの OS ではエンジンとして動かない。
    std::cerr << "pa-engine: this platform is not supported\n";
    return engine::kExitFatal;
#endif
}

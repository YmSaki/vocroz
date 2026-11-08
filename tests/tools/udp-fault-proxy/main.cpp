// SPDX-License-Identifier: AGPL-3.0-only
//
// vocroz-udp-fault-proxy: 実ソケットの UDP 障害注入プロキシ（Linux）。
// 子機と親機（またはランデブーサーバー）の間に置き、方向ごとに遅延、ジッター、欠損、重複、
// 順序の入れ替え、改ざんを注入する。シードを固定すれば、同じ送信の順序に対して同じ障害になる。
//
// 使い方:
//   vocroz-udp-fault-proxy --listen <ip:port> --upstream <ip:port>
//                          [--to-upstream <障害の設定>] [--to-client <障害の設定>]
//   障害の設定の書式は tests/README.md（例: --to-upstream delay=20ms,jitter=5ms,loss=0.02,seed=7）
// SIGINT か SIGTERM で終了し、統計を標準エラーに出す。

#include <csignal>
#include <cstdio>
#include <string_view>

#include "udp_fault_proxy.h"

namespace {

constexpr int kExitUsage = 2;

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int /*signal*/) {
    g_stop = 1;
}

int usage(const char* message) {
    std::fprintf(stderr, "vocroz-udp-fault-proxy: %s\n", message);
    std::fprintf(stderr,
                 "usage: vocroz-udp-fault-proxy --listen <ip:port> --upstream <ip:port> "
                 "[--to-upstream <faults>] [--to-client <faults>]\n");
    return kExitUsage;
}

}  // namespace

int main(int argc, char** argv) {
    vocroz::tools::UdpFaultProxyConfig config;
    bool has_listen = false;
    bool has_upstream = false;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            return usage("missing value for the last option");
        }
        const std::string_view name = argv[i];
        const std::string_view value = argv[i + 1];
        if (name == "--listen" || name == "--upstream") {
            const auto endpoint = vocroz::tools::parse_ipv4_endpoint(value);
            if (!endpoint) {
                return usage("invalid address (use ip:port)");
            }
            (name == "--listen" ? config.listen : config.upstream) = *endpoint;
            (name == "--listen" ? has_listen : has_upstream) = true;
        } else if (name == "--to-upstream" || name == "--to-client") {
            auto profile = vocroz::netsim::parse_fault_profile(value);
            if (!profile) {
                return usage(profile.error().c_str());
            }
            (name == "--to-upstream" ? config.to_upstream : config.to_client) = profile.value();
        } else {
            return usage("unknown option");
        }
    }
    if (!has_listen || !has_upstream) {
        return usage("--listen and --upstream are required");
    }
    auto proxy = vocroz::tools::UdpFaultProxy::create(config);
    if (!proxy) {
        return usage(proxy.error().c_str());
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::fprintf(stderr, "to-upstream: %s\nto-client: %s\n",
                 vocroz::netsim::format_fault_profile(config.to_upstream).c_str(),
                 vocroz::netsim::format_fault_profile(config.to_client).c_str());
    while (g_stop == 0) {
        proxy.value()->poll_once(std::chrono::milliseconds{100});
    }
    const auto& stats = proxy.value()->stats();
    std::fprintf(stderr,
                 "received %llu, forwarded %llu, lost %llu, duplicated %llu, corrupted %llu, "
                 "clients %llu\n",
                 static_cast<unsigned long long>(stats.received),
                 static_cast<unsigned long long>(stats.forwarded),
                 static_cast<unsigned long long>(stats.lost),
                 static_cast<unsigned long long>(stats.duplicated),
                 static_cast<unsigned long long>(stats.corrupted),
                 static_cast<unsigned long long>(stats.clients));
    return 0;
}

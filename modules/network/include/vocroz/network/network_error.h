// SPDX-License-Identifier: AGPL-3.0-only
//
// network が返すエラーの原因（AGENTS.md 6.2）。
// 接続できなかった原因を区別して返し、session が利用者に原因別の対処案
// （親機のポート開放、IPv6 の利用、別の回線での再試行など）を表示できるようにする。
#pragma once

namespace vocroz::network {

/// network の処理が失敗した原因。
enum class NetworkError {
    /// ソケットを作れなかった（Winsock の初期化の失敗を含む）。
    kSocketUnavailable,
    /// ポートにバインドできなかった（使用中など）。
    kBindFailed,
    /// データグラムを送れなかった。
    kSendFailed,
    /// 送信先のアドレスファミリーをこのソケットで扱えない（IPv4 だけのソケットで IPv6
    /// に送ろうとした）。
    kAddressFamilyUnsupported,
    /// ホスト名を解決できなかった。
    kResolveFailed,
    /// 応答が時間内に届かなかった。
    kTimedOut,
    /// 応答の形式が正しくない。
    kMalformedResponse,
    /// デフォルトゲートウェイが見つからない。
    kNoGateway,
    /// ゲートウェイが自動ポート開放に対応していない、または外から届かない（二重 NAT）。
    kPortMappingUnsupported,
    /// ゲートウェイがポートの割り当てを断った。
    kPortMappingRefused,
    /// ランデブーサーバーから応答がない。
    kRendezvousUnreachable,
    /// ランデブーサーバーが上限に達していて登録できない。
    kRendezvousCapacity,
    /// ランデブーサーバーに親機が登録していない（親機がセッションを開いていない）。
    kHostNotFound,
    /// 直結もホールパンチも成立しなかった。
    kUnreachable,
};

}  // namespace vocroz::network

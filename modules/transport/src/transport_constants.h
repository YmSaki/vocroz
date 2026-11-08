// SPDX-License-Identifier: AGPL-3.0-only
//
// transport の内部だけで使う定数。
// 値の根拠を各定数のコメントに書く。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "vocroz/transport/constants.h"

namespace vocroz::transport {

/// ワイヤーフォーマットのバージョン。形式を変えるときは値を上げ、古い値のパケットは捨てる。
inline constexpr std::uint8_t kProtocolVersion = 1;

/// 使う Noise プロトコル（docs/adr/0001-key-exchange-noise-ik.md）。
inline constexpr const char* kNoiseProtocolName = "Noise_IK_25519_ChaChaPoly_BLAKE2s";

/// Noise のプロローグの先頭に置くラベル。後ろにバージョンと接続 ID を続ける。
/// プロローグはハンドシェイクのハッシュに混ぜられるので、別の製品やバージョンとの取り違えと、
/// 平文のヘッダーにある接続 ID の書き換えを、ハンドシェイクの失敗として検出できる。
inline constexpr std::string_view kPrologueLabel = "VOCROZ transport";

/// ChaCha20-Poly1305（IETF）の nonce の長さ（バイト）。
inline constexpr std::size_t kAeadNonceBytes = 12;

/// Noise のハンドシェイクメッセージの一時公開鍵の長さ（バイト）。
inline constexpr std::size_t kEphemeralKeyBytes = 32;

/// 再生の開始時（ミュート解除後の区間の始まりと、無音からの復帰）のフェードインの長さ。
/// クリックノイズを防ぐランプの長さ 5〜20ms の中から、立ち上がりの遅れが目立たない 10ms にする。
inline constexpr std::size_t kFadeInSamples = kSampleRate / 100;

/// フレームが届かないときに PLC で補間し続ける長さの上限（40ms）。
/// 単発の欠損は PLC で隠すが、長い途絶で古い音を引き延ばして鳴らし続けないよう、
/// これを超えたら無音にする。
/// Opus の PLC は数十 ms を超えると不自然な持続音になるので、その手前で止める。
inline constexpr std::size_t kMaxConcealmentSamples = kSampleRate / 25;

/// PLC の上限の手前で無音へ落とすフェードアウトの長さ（10ms）。
/// 無音への切り替えでクリックノイズを出さないため。
inline constexpr std::size_t kConcealmentFadeSamples = kSampleRate / 100;

/// RECEIVING 中にこの時間フレームが届かなければ、回線が途絶したとみなして NO_SIGNAL にする。
inline constexpr std::chrono::milliseconds kReceiveTimeout{500};

/// 送信ミュートの解除通知のあと、この時間内に最初のフレームが届かなければ NO_SIGNAL にする。
/// 解除の通知（制御メッセージ）と最初のフレームの到着順が逆転したり、
/// 送信の再開に時間がかかったりしても、誤って NO_SIGNAL にしないための猶予。
inline constexpr std::chrono::seconds kResumeTimeout{1};

/// ドリフト補正で変えてよい再生レートの上限（±0.2%）。
/// 0.2% のピッチの変化は約 3.5 セントで、合奏で聞き分けられる量より小さい。
/// 水晶発振子の誤差（±100ppm 程度）の 20 倍あるので、
/// ドリフトを打ち消したうえで深さの調整にも使える。
inline constexpr double kMaxRateDeviation = 0.002;

/// ドリフト補正の比例ゲイン（1/秒）。
/// 深さの誤差が 5ms のときにレートを 0.1% 変える。
/// 誤差が縮む時定数は約 5 秒で、
/// ジッターによる深さの揺れにレートが追従してピッチが揺れるのを避ける。
inline constexpr double kRateProportionalGain = 0.2;

/// ドリフト補正の積分ゲイン（1/秒^2）。
/// 比例ゲインの 2 乗の 1/4 にして臨界減衰にし、一定のドリフトを定常偏差なしで打ち消しつつ、
/// 深さが振動しないようにする。
inline constexpr double kRateIntegralGain = kRateProportionalGain * kRateProportionalGain / 4.0;

/// 積分項（推定したドリフト）の上限（±0.1%）。
/// 実際の時計の誤差より十分大きく、暴走したときの上限として働く。
inline constexpr double kMaxDriftEstimate = 0.001;

/// 深さの平滑化の時定数（秒）。
/// フレーム単位の取り出しによる深さの鋸歯状の変化を均すため、5ms のフレームより十分長く、
/// ドリフト補正の時定数（約 5 秒）より十分短くする。
inline constexpr double kDepthSmoothingSeconds = 0.5;

/// 目標の深さを 1 フレーム浅くするまでに、枯渇が起きていないことを確かめる時間（10 秒）。
/// 深くするのは枯渇の直後にすぐ行い、浅くするのは慎重に行って、遅延と途切れの振動を避ける。
inline constexpr std::size_t kTargetDecreaseIntervalSamples = std::size_t{kSampleRate} * 10;

/// ネットワーク受信スレッドからオーディオスレッドへフレームを渡すキューの容量。
/// 2.5ms のフレームで 640ms 分あり、
/// オーディオスレッドが数ブロック遅れても新しいフレームを取りこぼさない。
inline constexpr std::size_t kInboundQueueCapacity = 256;

}  // namespace vocroz::transport

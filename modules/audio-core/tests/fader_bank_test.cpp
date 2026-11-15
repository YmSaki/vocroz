// SPDX-License-Identifier: AGPL-3.0-only
//
// FaderBank の単体テスト。初期ミュート、ミュートとゲインの合成、リセット、範囲外の扱い、
// ランプを進めない follow() を検証する。

#include "fader_bank.h"

#include <array>

#include <gtest/gtest.h>

namespace vocroz::audio_core {
namespace {

// 長さ frames の一定値 1 のモノラル信号にフェーダーを掛け、最後のサンプルを返す。
float process_ones(FaderBank& bank, FaderId fader, std::size_t frames = 8) {
    std::array<float, 8> samples{};
    samples.fill(1.0F);
    std::array<float*, 1> channels{samples.data()};
    bank.process(fader, channels.data(), 1, frames);
    return samples[frames - 1];
}

ParameterChange mute(FaderId fader, bool muted) {
    ParameterChange change;
    change.kind = ParameterChange::Kind::kMute;
    change.fader = fader;
    change.muted = muted;
    return change;
}

ParameterChange gain(FaderId fader, float value) {
    ParameterChange change;
    change.kind = ParameterChange::Kind::kGain;
    change.fader = fader;
    change.gain = value;
    return change;
}

TEST(FaderBankTest, StartsAllFadersMuted) {
    FaderBank bank(3, 2);
    for (std::uint16_t id = 0; id < 3; ++id) {
        EXPECT_EQ(process_ones(bank, FaderId{id}), 0.0F);
    }
}

TEST(FaderBankTest, UnmuteRampsToUnityAndGainKeepsMuteIndependent) {
    FaderBank bank(1, 2);
    const FaderId fader{0};
    bank.apply(gain(fader, 0.5F));
    EXPECT_EQ(process_ones(bank, fader), 0.0F);  // ミュート中のゲイン変更では音が出ない
    bank.apply(mute(fader, false));
    EXPECT_FLOAT_EQ(process_ones(bank, fader), 0.5F);
    bank.apply(mute(fader, true));
    EXPECT_EQ(process_ones(bank, fader), 0.0F);
}

TEST(FaderBankTest, ResetMutesImmediatelyAndRestoresUnityGain) {
    FaderBank bank(1, 480);
    const FaderId fader{0};
    bank.apply(gain(fader, 2.0F));
    bank.apply(mute(fader, false));
    (void)process_ones(bank, fader);

    ParameterChange reset;
    reset.kind = ParameterChange::Kind::kReset;
    reset.fader = fader;
    bank.apply(reset);
    // ランプを待たずに、最初のサンプルから無音になる。
    std::array<float, 1> sample{1.0F};
    std::array<float*, 1> channels{sample.data()};
    bank.process(fader, channels.data(), 1, 1);
    EXPECT_EQ(sample[0], 0.0F);

    bank.apply(mute(fader, false));
    float last = 0.0F;
    for (int i = 0; i < 100; ++i) {
        last = process_ones(bank, fader);
    }
    EXPECT_FLOAT_EQ(last, 1.0F);  // 解除すると 0 dB に戻る
}

TEST(FaderBankTest, SilencesUnknownFaderAndIgnoresOutOfRangeChanges) {
    FaderBank bank(1, 2);
    bank.apply(mute(FaderId{5}, false));
    EXPECT_EQ(process_ones(bank, FaderId{5}), 0.0F);
    EXPECT_EQ(process_ones(bank, FaderId{0}), 0.0F);
}

// 一定値 1 のモノラル 8 サンプルに、process() か follow() でフェーダーを掛けた結果を返す。
std::array<float, 8> ramp_of(FaderBank& bank, FaderId fader, bool follow) {
    std::array<float, 8> samples{};
    samples.fill(1.0F);
    std::array<float*, 1> channels{samples.data()};
    if (follow) {
        bank.follow(fader, channels.data(), 1, samples.size());
    } else {
        bank.process(fader, channels.data(), 1, samples.size());
    }
    return samples;
}

TEST(FaderBankTest, FollowMatchesProcessInTheSameBlockInEitherOrder) {
    FaderBank bank(1, 16);
    const FaderId fader{0};
    bank.apply(mute(fader, false));

    // ランプの途中でも、同じブロックなら process() の前後どちらで follow() しても同じゲイン列。
    bank.begin_block();
    const auto followed_before = ramp_of(bank, fader, true);
    const auto processed = ramp_of(bank, fader, false);
    EXPECT_EQ(followed_before, processed);

    bank.begin_block();
    const auto processed_next = ramp_of(bank, fader, false);
    const auto followed_after = ramp_of(bank, fader, true);
    EXPECT_EQ(followed_after, processed_next);
    // follow() はランプを進めないので、2 ブロック目は 1 ブロック目の続きになる。
    EXPECT_GT(processed_next[0], processed[7]);
}

TEST(FaderBankTest, FollowSilencesOutOfRangeFader) {
    FaderBank bank(1, 2);
    bank.begin_block();
    const auto samples = ramp_of(bank, FaderId{5}, true);
    EXPECT_EQ(samples[0], 0.0F);
    EXPECT_EQ(samples[7], 0.0F);
}

}  // namespace
}  // namespace vocroz::audio_core

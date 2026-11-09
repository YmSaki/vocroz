// SPDX-License-Identifier: AGPL-3.0-only
//
// playout_stream.h の実装。
// 再生は「溜める（kBuffering）」と「再生する（kPlaying）」の 2 つの状態を行き来する。
// 溜めている間は無音を出し、深さが目標に達したら最も古いフレームから再生を始めてフェードインする。
// 再生中にフレームが途絶えたら PLC で補い、上限（40ms）を超えたらフェードアウトして無音にし、
// 溜め直す。
// ステレオのストリームは、インターリーブでデコードしてからチャンネルごとのリサンプラーに分ける。
// 2 つのリサンプラーには同じ長さを積み、同じ変換比で取り出すので、左右の位置はずれない。

#include "vocroz/transport/playout_stream.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <vector>

#include "vocroz/audio_core/realtime.h"
#include "vocroz/common/spsc_queue.h"
#include "vocroz/transport/opus_frame_decoder.h"

#include "drift_controller.h"
#include "drift_resampler.h"
#include "jitter_buffer.h"
#include "transport_constants.h"

namespace vocroz::transport {

namespace {

// 目標の深さの上限として受け付ける最大値。
// 仕様の可変範囲の上限（250ms）に合わせる。
constexpr std::chrono::microseconds kLongestMaxDelay{250'000};
// ジッターバッファの容量（上限の深さに対する倍数）。
// 上限を超えてまとめて届いたフレームも、
// 古いものを捨てる判断をするまでは並べ直しにせず格納できるようにする。
constexpr std::size_t kCapacityPerMaxDelay = 4;
// 目標を浅くしてよいと判断する、ジッターバッファに常に残っていた深さ（フレーム数）。
// 1 フレーム浅くしても、なお 1 フレームの余裕が残ることを条件にする。
constexpr std::size_t kSlackFramesToDecrease = 2;

std::size_t to_samples(std::chrono::microseconds duration) noexcept {
    const auto count = static_cast<std::uint64_t>(std::max<std::int64_t>(duration.count(), 0));
    return static_cast<std::size_t>(count * kSampleRate / 1'000'000);
}

std::size_t round_up_to_frames(std::size_t samples, std::size_t frame_samples) noexcept {
    return (samples + frame_samples - 1) / frame_samples * frame_samples;
}

}  // namespace

struct PlayoutStream::Impl {
    enum class Mode : std::uint8_t { kBuffering, kPlaying };

    Impl(std::size_t frame_samples, std::size_t min_delay, std::size_t max_delay,
         OpusFrameDecoder decoder)
        : frame_samples(frame_samples),
          channels(decoder.channels()),
          min_delay(min_delay),
          max_delay(max_delay),
          target(min_delay),
          jitter(frame_samples, max_delay / frame_samples * kCapacityPerMaxDelay + 1),
          decoder(std::move(decoder)),
          interleaved(frame_samples * channels) {
        for (std::size_t ch = 0; ch < channels; ++ch) {
            resamplers.emplace_back(frame_samples);
            frame_buffers.emplace_back(frame_samples);
        }
    }

    // チャンネルごとの書き込み先。使うのは先頭の channels 個。
    using Outputs = std::array<std::span<float>, kMaxStreamChannels>;

    void render(const Outputs& outs, std::size_t frames) noexcept VOCROZ_NONBLOCKING;

    void drain() noexcept VOCROZ_NONBLOCKING;
    void accept(const MediaFrame& frame) noexcept VOCROZ_NONBLOCKING;
    void begin_segment(std::uint32_t epoch) noexcept VOCROZ_NONBLOCKING;
    void start_playout() noexcept VOCROZ_NONBLOCKING;
    void enter_rebuffer() noexcept VOCROZ_NONBLOCKING;
    // 次の 1 フレームを PCM にしてリサンプラーに積む。
    // 無音に切り替えたら false。
    bool decode_next() noexcept VOCROZ_NONBLOCKING;
    bool conceal_next() noexcept VOCROZ_NONBLOCKING;
    // インターリーブの PCM をチャンネルごとに分ける。
    void deinterleave() noexcept VOCROZ_NONBLOCKING;
    void push_frame() noexcept VOCROZ_NONBLOCKING;
    void reset_resamplers() noexcept VOCROZ_NONBLOCKING;
    void apply_fade_in(const Outputs& outs, std::size_t offset,
                       std::size_t count) noexcept VOCROZ_NONBLOCKING;
    void raise_target() noexcept VOCROZ_NONBLOCKING;
    void update_after_render(std::size_t rendered) noexcept VOCROZ_NONBLOCKING;
    [[nodiscard]] double total_depth() const noexcept VOCROZ_NONBLOCKING;

    const std::size_t frame_samples;
    const std::size_t channels;
    const std::size_t min_delay;
    const std::size_t max_delay;
    std::size_t target;

    JitterBuffer jitter;
    // チャンネルごとのリサンプラー。
    std::vector<DriftResampler> resamplers;
    DriftController controller;
    OpusFrameDecoder decoder;
    common::SpscQueue<MediaFrame, kInboundQueueCapacity> queue;
    // デコードしたインターリーブの 1 フレーム。
    std::vector<float> interleaved;
    // チャンネルごとの 1 フレーム。
    std::vector<std::vector<float>> frame_buffers;

    Mode mode = Mode::kBuffering;
    // 現在の区間のミュートの世代番号。
    // 0 は「まだ区間がない」（初期状態は送信ミュート）。
    std::uint32_t segment_epoch = 0;
    // 連続して PLC で補間したサンプル数。
    std::size_t concealed = 0;
    // フェードインの残りのサンプル数。
    std::size_t fade_in_remaining = 0;
    // 目標を浅くするかの判断に使う、前回の調整からの経過と、その間のジッターバッファの最小の深さ。
    std::size_t samples_since_adjust = 0;
    std::size_t min_depth_since_adjust = std::numeric_limits<std::size_t>::max();

    PlayoutStats stats;
    // 生産者スレッドだけが増やす。
    // stats() で読む。
    std::atomic<std::uint64_t> queue_full{0};
};

void PlayoutStream::Impl::drain() noexcept VOCROZ_NONBLOCKING {
    while (const auto frame = queue.try_pop()) {
        accept(*frame);
    }
    if (mode == Mode::kPlaying && jitter.depth_samples() > max_delay) {
        // 回線の停滞のあとにまとめて届いたフレームを順に再生すると、
        // 遅れたままの古い音が鳴り続ける。
        // 目標の深さまで古いほうを捨てて、遅延を戻す。
        stats.frames_dropped_overflow += jitter.drop_until(target);
    }
}

void PlayoutStream::Impl::accept(const MediaFrame& frame) noexcept VOCROZ_NONBLOCKING {
    if (frame.mute_epoch < segment_epoch) {
        ++stats.frames_stale_epoch;
        return;
    }
    if (frame.mute_epoch > segment_epoch) {
        begin_segment(frame.mute_epoch);
    }
    switch (jitter.insert(frame)) {
        case InsertResult::kAccepted:
            return;
        case InsertResult::kDuplicate:
            ++stats.frames_duplicate;
            return;
        case InsertResult::kLate:
            ++stats.frames_late;
            return;
        case InsertResult::kMisaligned:
            ++stats.frames_misaligned;
            return;
        case InsertResult::kDiscontinuity:
            // 再生位置から大きく離れたフレームで並べ直したので、溜め直してから再生する。
            mode = Mode::kBuffering;
            ++stats.rebuffers;
            return;
    }
}

void PlayoutStream::Impl::begin_segment(std::uint32_t epoch) noexcept VOCROZ_NONBLOCKING {
    // ミュート解除後の新しい区間。
    // 停止前のデータを再生しないよう、バッファとデコーダーを初期化する。
    // ドリフトの推定値は同じ送信側の時計のものなので保持する（DriftController::restart()）。
    segment_epoch = epoch;
    jitter.reset();
    decoder.reset();
    reset_resamplers();
    controller.restart();
    mode = Mode::kBuffering;
    ++stats.segments;
}

void PlayoutStream::Impl::start_playout() noexcept VOCROZ_NONBLOCKING {
    jitter.start();
    stats.frames_dropped_overflow += jitter.drop_until(target);
    reset_resamplers();
    controller.restart();
    concealed = 0;
    fade_in_remaining = kFadeInSamples;
    samples_since_adjust = 0;
    min_depth_since_adjust = std::numeric_limits<std::size_t>::max();
    mode = Mode::kPlaying;
}

void PlayoutStream::Impl::enter_rebuffer() noexcept VOCROZ_NONBLOCKING {
    jitter.rebuffer();
    decoder.reset();
    reset_resamplers();
    mode = Mode::kBuffering;
    ++stats.rebuffers;
}

bool PlayoutStream::Impl::decode_next() noexcept VOCROZ_NONBLOCKING {
    const PopResult popped = jitter.pop();
    switch (popped.kind) {
        case PopKind::kFrame: {
            const auto decoded = decoder.decode(popped.frame->payload_bytes(), interleaved);
            if (decoded && decoded.value() == frame_samples) {
                concealed = 0;
                ++stats.frames_decoded;
                deinterleave();
                push_frame();
                return true;
            }
            // 壊れたパケットや、設定と違うフレーム長のパケットは欠損として扱う。
            return conceal_next();
        }
        case PopKind::kMissing:
            return conceal_next();
        case PopKind::kEmpty:
            ++stats.underruns;
            raise_target();
            return conceal_next();
    }
    return conceal_next();
}

bool PlayoutStream::Impl::conceal_next() noexcept VOCROZ_NONBLOCKING {
    if (concealed >= kMaxConcealmentSamples) {
        enter_rebuffer();
        return false;
    }
    if (!decoder.conceal(interleaved)) {
        std::ranges::fill(interleaved, 0.0F);
    }
    deinterleave();
    // PLC の上限の手前で無音までフェードアウトする。
    // 単発の欠損（上限よりずっと短い）には影響しない。
    const std::size_t fade_start = kMaxConcealmentSamples - kConcealmentFadeSamples;
    for (auto& buffer : frame_buffers) {
        for (std::size_t i = 0; i < buffer.size(); ++i) {
            const std::size_t position = concealed + i;
            if (position >= kMaxConcealmentSamples) {
                buffer[i] = 0.0F;
            } else if (position >= fade_start) {
                const auto remaining = static_cast<float>(kMaxConcealmentSamples - position);
                buffer[i] *= remaining / static_cast<float>(kConcealmentFadeSamples);
            }
        }
    }
    concealed += frame_samples;
    ++stats.frames_concealed;
    push_frame();
    return true;
}

void PlayoutStream::Impl::deinterleave() noexcept VOCROZ_NONBLOCKING {
    for (std::size_t ch = 0; ch < channels; ++ch) {
        std::vector<float>& buffer = frame_buffers[ch];
        for (std::size_t i = 0; i < frame_samples; ++i) {
            buffer[i] = interleaved[(i * channels) + ch];
        }
    }
}

void PlayoutStream::Impl::push_frame() noexcept VOCROZ_NONBLOCKING {
    for (std::size_t ch = 0; ch < channels; ++ch) {
        static_cast<void>(resamplers[ch].push(frame_buffers[ch]));
    }
}

void PlayoutStream::Impl::reset_resamplers() noexcept VOCROZ_NONBLOCKING {
    for (auto& resampler : resamplers) {
        resampler.reset();
    }
}

void PlayoutStream::Impl::apply_fade_in(const Outputs& outs, std::size_t offset,
                                        std::size_t count) noexcept VOCROZ_NONBLOCKING {
    for (std::size_t i = offset; i < offset + count; ++i) {
        if (fade_in_remaining == 0) {
            return;
        }
        const auto done = static_cast<float>(kFadeInSamples - fade_in_remaining);
        const float gain = done / static_cast<float>(kFadeInSamples);
        for (std::size_t ch = 0; ch < channels; ++ch) {
            outs[ch][i] *= gain;
        }
        --fade_in_remaining;
    }
}

void PlayoutStream::Impl::render(const Outputs& outs,
                                 std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    drain();
    std::size_t produced = 0;
    while (produced < frames) {
        if (mode == Mode::kBuffering) {
            if (jitter.stored_frames() == 0 || jitter.depth_samples() < target) {
                for (std::size_t ch = 0; ch < channels; ++ch) {
                    std::fill(outs[ch].begin() + static_cast<std::ptrdiff_t>(produced),
                              outs[ch].end(), 0.0F);
                }
                jitter.advance_floor(frames - produced);
                break;
            }
            start_playout();
        }
        // どのチャンネルにも同じ長さを積んでいるので、取り出せる長さも同じになる。
        const std::size_t pulled =
            resamplers[0].pull(outs[0].subspan(produced), controller.ratio());
        for (std::size_t ch = 1; ch < channels; ++ch) {
            static_cast<void>(
                resamplers[ch].pull(outs[ch].subspan(produced, pulled), controller.ratio()));
        }
        apply_fade_in(outs, produced, pulled);
        produced += pulled;
        if (produced < frames) {
            // 溜め直しに入ったら次の周回で無音を埋める。
            static_cast<void>(decode_next());
        }
    }
    update_after_render(frames);
}

void PlayoutStream::Impl::raise_target() noexcept VOCROZ_NONBLOCKING {
    // 枯渇はジッターが目標の深さを超えたことを意味するので、すぐに 1 フレーム深くする。
    target = std::min(target + frame_samples, max_delay);
    samples_since_adjust = 0;
    min_depth_since_adjust = std::numeric_limits<std::size_t>::max();
}

void PlayoutStream::Impl::update_after_render(std::size_t rendered) noexcept VOCROZ_NONBLOCKING {
    if (mode != Mode::kPlaying) {
        return;
    }
    stats.rate_ratio = controller.update(
        total_depth(), static_cast<double>(target + DriftResampler::kHalfTaps), rendered);

    min_depth_since_adjust = std::min(min_depth_since_adjust, jitter.depth_samples());
    samples_since_adjust += rendered;
    if (samples_since_adjust >= kTargetDecreaseIntervalSamples) {
        // 一定時間、枯渇せずに余裕が残り続けていたら、遅延を減らすため 1 フレーム浅くする。
        if (target > min_delay &&
            min_depth_since_adjust >= frame_samples * kSlackFramesToDecrease) {
            target = std::max(target - frame_samples, min_delay);
        }
        samples_since_adjust = 0;
        min_depth_since_adjust = std::numeric_limits<std::size_t>::max();
    }
}

double PlayoutStream::Impl::total_depth() const noexcept VOCROZ_NONBLOCKING {
    return static_cast<double>(jitter.depth_samples()) + resamplers[0].buffered_samples();
}

common::Result<std::unique_ptr<PlayoutStream>, PlayoutError> PlayoutStream::create(
    const PlayoutConfig& config) {
    const std::size_t frame = frame_samples(config.frame_duration);
    if (config.min_delay > config.max_delay || config.max_delay > kLongestMaxDelay ||
        to_samples(config.min_delay) < frame || config.channels == 0 ||
        config.channels > kMaxStreamChannels) {
        return common::make_unexpected(PlayoutError::kInvalidConfig);
    }
    auto decoder = OpusFrameDecoder::create(config.channels);
    if (!decoder) {
        return common::make_unexpected(PlayoutError::kCodecFailed);
    }
    // 深さはフレーム単位でしか変わらないので、目標の下限と上限をフレーム長の倍数に切り上げる。
    const std::size_t min_delay = round_up_to_frames(to_samples(config.min_delay), frame);
    const std::size_t max_delay = round_up_to_frames(to_samples(config.max_delay), frame);
    auto impl = std::make_unique<Impl>(frame, min_delay, max_delay, std::move(decoder).value());
    return std::make_unique<PlayoutStream>(ConstructionKey{}, std::move(impl));
}

PlayoutStream::PlayoutStream(ConstructionKey /*key*/, std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

PlayoutStream::~PlayoutStream() = default;

bool PlayoutStream::enqueue(const MediaFrame& frame) noexcept {
    if (impl_->queue.try_push(frame)) {
        return true;
    }
    impl_->queue_full.fetch_add(1, std::memory_order_relaxed);
    return false;
}

void PlayoutStream::render(std::span<float> out) noexcept VOCROZ_NONBLOCKING {
    if (impl_->channels != 1) {
        std::ranges::fill(out, 0.0F);
        return;
    }
    impl_->render({out, {}}, out.size());
}

void PlayoutStream::render(std::span<float> left,
                           std::span<float> right) noexcept VOCROZ_NONBLOCKING {
    if (impl_->channels != 2 || left.size() != right.size()) {
        std::ranges::fill(left, 0.0F);
        std::ranges::fill(right, 0.0F);
        return;
    }
    impl_->render({left, right}, left.size());
}

std::size_t PlayoutStream::channels() const noexcept {
    return impl_->channels;
}

PlayoutStats PlayoutStream::stats() const noexcept {
    PlayoutStats result = impl_->stats;
    result.frames_queue_full = impl_->queue_full.load(std::memory_order_relaxed);
    result.target_delay_samples = impl_->target;
    result.depth_samples = impl_->total_depth();
    result.drift_estimate = impl_->controller.drift_estimate();
    result.rate_ratio = impl_->controller.ratio();
    result.playing = impl_->mode == Impl::Mode::kPlaying;
    return result;
}

}  // namespace vocroz::transport

// SPDX-License-Identifier: AGPL-3.0-only
//
// playout_stream.h の実装。
// 再生は「溜める（kBuffering）」と「再生する（kPlaying）」の 2 つの状態を行き来する。
// 溜めている間は無音を出し、深さが目標に達したら最も古いフレームから再生を始めてフェードインする。
// 再生中にフレームが途絶えたら PLC で補い、上限（40ms）を超えたらフェードアウトして無音にし、
// 溜め直す。

#include "vocroz/transport/playout_stream.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <vector>

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
          min_delay(min_delay),
          max_delay(max_delay),
          target(min_delay),
          jitter(frame_samples, max_delay / frame_samples * kCapacityPerMaxDelay + 1),
          resampler(frame_samples),
          decoder(std::move(decoder)),
          frame_buffer(frame_samples) {}

    void drain() noexcept;
    void accept(const MediaFrame& frame) noexcept;
    void begin_segment(std::uint32_t epoch) noexcept;
    void start_playout() noexcept;
    void enter_rebuffer() noexcept;
    // 次の 1 フレームを PCM にしてリサンプラーに積む。
    // 無音に切り替えたら false。
    bool decode_next() noexcept;
    bool conceal_next() noexcept;
    void apply_fade_in(std::span<float> block) noexcept;
    void raise_target() noexcept;
    void update_after_render(std::size_t rendered) noexcept;
    [[nodiscard]] double total_depth() const noexcept;

    const std::size_t frame_samples;
    const std::size_t min_delay;
    const std::size_t max_delay;
    std::size_t target;

    JitterBuffer jitter;
    DriftResampler resampler;
    DriftController controller;
    OpusFrameDecoder decoder;
    common::SpscQueue<MediaFrame, kInboundQueueCapacity> queue;
    std::vector<float> frame_buffer;

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

void PlayoutStream::Impl::drain() noexcept {
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

void PlayoutStream::Impl::accept(const MediaFrame& frame) noexcept {
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

void PlayoutStream::Impl::begin_segment(std::uint32_t epoch) noexcept {
    // ミュート解除後の新しい区間。
    // 停止前のデータを再生しないよう、バッファとデコーダーを初期化する。
    // ドリフトの推定値は同じ送信側の時計のものなので保持する（DriftController::restart()）。
    segment_epoch = epoch;
    jitter.reset();
    decoder.reset();
    resampler.reset();
    controller.restart();
    mode = Mode::kBuffering;
    ++stats.segments;
}

void PlayoutStream::Impl::start_playout() noexcept {
    jitter.start();
    stats.frames_dropped_overflow += jitter.drop_until(target);
    resampler.reset();
    controller.restart();
    concealed = 0;
    fade_in_remaining = kFadeInSamples;
    samples_since_adjust = 0;
    min_depth_since_adjust = std::numeric_limits<std::size_t>::max();
    mode = Mode::kPlaying;
}

void PlayoutStream::Impl::enter_rebuffer() noexcept {
    jitter.rebuffer();
    decoder.reset();
    resampler.reset();
    mode = Mode::kBuffering;
    ++stats.rebuffers;
}

bool PlayoutStream::Impl::decode_next() noexcept {
    const PopResult popped = jitter.pop();
    switch (popped.kind) {
        case PopKind::kFrame: {
            const auto decoded = decoder.decode(popped.frame->payload_bytes(), frame_buffer);
            if (decoded && decoded.value() == frame_samples) {
                concealed = 0;
                ++stats.frames_decoded;
                static_cast<void>(resampler.push(frame_buffer));
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

bool PlayoutStream::Impl::conceal_next() noexcept {
    if (concealed >= kMaxConcealmentSamples) {
        enter_rebuffer();
        return false;
    }
    if (!decoder.conceal(frame_buffer)) {
        std::ranges::fill(frame_buffer, 0.0F);
    }
    // PLC の上限の手前で無音までフェードアウトする。
    // 単発の欠損（上限よりずっと短い）には影響しない。
    const std::size_t fade_start = kMaxConcealmentSamples - kConcealmentFadeSamples;
    for (std::size_t i = 0; i < frame_buffer.size(); ++i) {
        const std::size_t position = concealed + i;
        if (position >= kMaxConcealmentSamples) {
            frame_buffer[i] = 0.0F;
        } else if (position >= fade_start) {
            const auto remaining = static_cast<float>(kMaxConcealmentSamples - position);
            frame_buffer[i] *= remaining / static_cast<float>(kConcealmentFadeSamples);
        }
    }
    concealed += frame_samples;
    ++stats.frames_concealed;
    static_cast<void>(resampler.push(frame_buffer));
    return true;
}

void PlayoutStream::Impl::apply_fade_in(std::span<float> block) noexcept {
    for (float& sample : block) {
        if (fade_in_remaining == 0) {
            return;
        }
        const auto done = static_cast<float>(kFadeInSamples - fade_in_remaining);
        sample *= done / static_cast<float>(kFadeInSamples);
        --fade_in_remaining;
    }
}

void PlayoutStream::Impl::raise_target() noexcept {
    // 枯渇はジッターが目標の深さを超えたことを意味するので、すぐに 1 フレーム深くする。
    target = std::min(target + frame_samples, max_delay);
    samples_since_adjust = 0;
    min_depth_since_adjust = std::numeric_limits<std::size_t>::max();
}

void PlayoutStream::Impl::update_after_render(std::size_t rendered) noexcept {
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

double PlayoutStream::Impl::total_depth() const noexcept {
    return static_cast<double>(jitter.depth_samples()) + resampler.buffered_samples();
}

common::Result<std::unique_ptr<PlayoutStream>, PlayoutError> PlayoutStream::create(
    const PlayoutConfig& config) {
    const std::size_t frame = frame_samples(config.frame_duration);
    if (config.min_delay > config.max_delay || config.max_delay > kLongestMaxDelay ||
        to_samples(config.min_delay) < frame) {
        return common::make_unexpected(PlayoutError::kInvalidConfig);
    }
    auto decoder = OpusFrameDecoder::create();
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

void PlayoutStream::render(std::span<float> out) noexcept {
    Impl& s = *impl_;
    s.drain();
    std::size_t produced = 0;
    while (produced < out.size()) {
        if (s.mode == Impl::Mode::kBuffering) {
            if (s.jitter.stored_frames() == 0 || s.jitter.depth_samples() < s.target) {
                const std::size_t remaining = out.size() - produced;
                std::fill(out.begin() + static_cast<std::ptrdiff_t>(produced), out.end(), 0.0F);
                s.jitter.advance_floor(remaining);
                break;
            }
            s.start_playout();
        }
        const std::size_t pulled = s.resampler.pull(out.subspan(produced), s.controller.ratio());
        s.apply_fade_in(out.subspan(produced, pulled));
        produced += pulled;
        if (produced < out.size()) {
            // 溜め直しに入ったら次の周回で無音を埋める。
            static_cast<void>(s.decode_next());
        }
    }
    s.update_after_render(out.size());
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

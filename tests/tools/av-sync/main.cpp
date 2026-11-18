// SPDX-License-Identifier: AGPL-3.0-only
//
// 光とクリックの試験素材を作り、時間差を測ります。
// 使い方: vocroz-av-sync generate --out-dir DIR / measure --video VIDEO --audio AUDIO
#include <charconv>
#include <iomanip>
#include <iostream>
#include <map>
#include <string_view>

#include "av_sync_measurement.h"
#include "av_sync_pattern.h"
#include "wav_file.h"
#include "y4m_file.h"

namespace vocroz::tools {
namespace {
using Options = std::map<std::string_view, std::string_view>;
int usage() {
    std::cerr << "Usage: vocroz-av-sync generate --out-dir DIR [--seconds N] [--fps N] "
                 "[--interval-ms N] [--audio-offset-ms N]\n"
              << "       vocroz-av-sync measure --video FILE --audio FILE\n";
    return 1;
}
int generate(Options opts) {
    if (!opts.contains("--out-dir"))
        return usage();
    const std::filesystem::path dir(opts.at("--out-dir"));
    opts.erase("--out-dir");
    AvSyncPattern p;
    const std::map<std::string_view, int*> fields{{"--seconds", &p.seconds},
                                                  {"--fps", &p.fps},
                                                  {"--interval-ms", &p.interval_ms},
                                                  {"--audio-offset-ms", &p.audio_offset_ms}};
    for (const auto& [key, value] : opts) {
        auto field = fields.find(key);
        if (field == fields.end())
            return usage();
        const auto end = value.data() + value.size();
        const auto r = std::from_chars(value.data(), end, *field->second);
        if (r.ec != std::errc{} || r.ptr != end)
            return usage();
    }
    const auto valid = validate(p);
    if (!valid) {
        switch (valid.error()) {
            case AvSyncPatternError::kInvalidValue:
                std::cerr << "Invalid pattern value\n";
                break;
            case AvSyncPatternError::kNotFrameAligned:
                std::cerr << "Interval or flash is not frame aligned\n";
                break;
            case AvSyncPatternError::kOffsetTooLarge:
                std::cerr << "Audio offset exceeds pairing range\n";
                break;
        }
        return usage();
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec ||
        !write_y4m_flat(dir / "av-sync-pattern.y4m", generate_frame_lumas(p), p.width, p.height,
                        p.fps) ||
        !write_wav_mono16(dir / "av-sync-pattern.wav", generate_audio(p), p.sample_rate)) {
        std::cerr << "Cannot write pattern files\n";
        return 2;
    }
    return 0;
}
int measure(const Options& opts) {
    if (opts.size() != 2 || !opts.contains("--video") || !opts.contains("--audio"))
        return usage();
    auto result = measure_files(opts.at("--video"), opts.at("--audio"), {});
    if (!result) {
        std::cerr << "Cannot read measurement files\n";
        return 2;
    }
    const auto& [report, s] = result.value();
    std::cout << std::fixed << std::setprecision(2) << "pairs=" << s.count
              << "\nunmatched_video=" << report.unmatched_video
              << "\nunmatched_audio=" << report.unmatched_audio << "\nmin_ms=" << s.min_ms
              << "\nmax_ms=" << s.max_ms << "\nmean_ms=" << s.mean_ms
              << "\nmedian_ms=" << s.median_ms << "\np95_abs_ms=" << s.p95_abs_ms << '\n';
    constexpr int kNoPairs = 3;
    return s.count ? 0 : kNoPairs;
}
}  // namespace
}  // namespace vocroz::tools
int main(int argc, char** argv) {
    using namespace vocroz::tools;
    if (argc < 2 || argc % 2)
        return usage();
    Options opts;
    for (int i = 2; i < argc; i += 2) {
        const std::string_view value(argv[i + 1]);
        if (value.empty() || value.starts_with("--") || !opts.emplace(argv[i], value).second)
            return usage();
    }
    const std::string_view command(argv[1]);
    if (command == "generate")
        return generate(opts);
    if (command == "measure")
        return measure(opts);
    return usage();
}

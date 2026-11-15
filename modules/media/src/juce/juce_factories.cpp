// SPDX-License-Identifier: AGPL-3.0-only
//
// factories.h の実装。std::filesystem::path を JUCE のファイルに写し、具象クラスを作る。

#include <optional>

#include <juce_audio_formats/juce_audio_formats.h>

#include "vocroz/media/factories.h"

#include "detail/free_space_guard.h"
#include "juce/juce_audio_file_reader.h"
#include "juce/juce_audio_file_writer.h"
#include "media_constants.h"

namespace vocroz::media {
namespace {

[[nodiscard]] juce::File to_juce_file(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return juce::File(juce::String::fromUTF8(reinterpret_cast<const char*>(utf8.c_str()),
                                             static_cast<int>(utf8.size())));
}

}  // namespace

common::Result<std::unique_ptr<audio_core::AudioFileWriter>, audio_core::AudioFileError>
open_audio_file_writer(const std::filesystem::path& file, audio_core::AudioFileFormat format,
                       std::size_t channels) {
    const juce::File target = to_juce_file(file);
    // ボリュームの空きは、ファイルの親フォルダで調べる（ファイルはまだないことがあるため）。
    const juce::File folder = target.getParentDirectory();
    detail::FreeSpaceGuard guard(
        [folder]() -> std::optional<std::uint64_t> {
            const juce::int64 free = folder.getBytesFreeOnVolume();
            // JUCE は調べられなかったときに 0 を返す。0 を容量不足と取り違えて録音を止めないよう、
            // 調べられなかったものとして扱う。本当に 0 なら書き込みが失敗した時点で止まる。
            if (free <= 0) {
                return std::nullopt;
            }
            return static_cast<std::uint64_t>(free);
        },
        kMinFreeDiskBytes, kFreeSpaceCheckBytes);
    auto writer = JuceAudioFileWriter::open(target, format, channels, std::move(guard));
    if (!writer) {
        return common::make_unexpected(writer.error());
    }
    return std::unique_ptr<audio_core::AudioFileWriter>(std::move(writer.value()));
}

common::Result<std::unique_ptr<audio_core::AudioFileReader>, audio_core::AudioFileError>
open_audio_file_reader(const std::filesystem::path& file) {
    auto reader = JuceAudioFileReader::open(to_juce_file(file));
    if (!reader) {
        return common::make_unexpected(reader.error());
    }
    return std::unique_ptr<audio_core::AudioFileReader>(std::move(reader.value()));
}

}  // namespace vocroz::media

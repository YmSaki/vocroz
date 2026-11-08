// SPDX-License-Identifier: AGPL-3.0-only
//
// 長さ付き JSON のフレームの組み立てと分解。

#include "vocroz/ipc/frame_codec.h"

#include <cstdint>

#include "vocroz/control_protocol/constants.h"

namespace vocroz::ipc {
namespace {

// 長さはリトルエンディアンで書く。x86 と ARM の Windows はどちらもリトルエンディアンだが、
// ホストのバイト順に頼らず 1 バイトずつ組み立て、形式をコードで明示する。
std::uint32_t read_length(std::string_view header) {
    std::uint32_t length = 0;
    for (std::size_t i = 0; i < kFrameHeaderBytes; ++i) {
        length |= static_cast<std::uint32_t>(static_cast<unsigned char>(header[i])) << (8U * i);
    }
    return length;
}

}  // namespace

common::Result<std::string, FrameError> encode_frame(std::string_view message) {
    if (message.size() > control_protocol::kMaxMessageBytes) {
        return common::make_unexpected(FrameError::kTooLarge);
    }
    const auto length = static_cast<std::uint32_t>(message.size());
    std::string frame;
    frame.reserve(kFrameHeaderBytes + message.size());
    for (std::size_t i = 0; i < kFrameHeaderBytes; ++i) {
        frame.push_back(static_cast<char>((length >> (8U * i)) & 0xFFU));
    }
    frame.append(message);
    return frame;
}

common::Result<void, FrameError> FrameDecoder::feed(std::span<const std::byte> bytes,
                                                    const MessageSink& on_message) {
    if (failed_) {
        return common::make_unexpected(FrameError::kTooLarge);
    }
    buffer_.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());

    std::size_t offset = 0;
    while (buffer_.size() - offset >= kFrameHeaderBytes) {
        const std::string_view rest = std::string_view{buffer_}.substr(offset);
        const std::uint32_t length = read_length(rest);
        if (length > control_protocol::kMaxMessageBytes) {
            // 上限を超える本文はメモリに読み込まない。境界も信用できないので、保持分も捨てる。
            failed_ = true;
            buffer_.clear();
            buffer_.shrink_to_fit();
            return common::make_unexpected(FrameError::kTooLarge);
        }
        if (rest.size() - kFrameHeaderBytes < length) {
            break;
        }
        on_message(rest.substr(kFrameHeaderBytes, length));
        offset += kFrameHeaderBytes + length;
    }
    buffer_.erase(0, offset);
    return {};
}

}  // namespace vocroz::ipc

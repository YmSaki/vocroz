// SPDX-License-Identifier: AGPL-3.0-only
//
// 長さ付き JSON のフレームの組み立てと分解の単体テスト。

#include "vocroz/ipc/frame_codec.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "vocroz/control_protocol/constants.h"

namespace vocroz::ipc {
namespace {

std::span<const std::byte> as_bytes(const std::string& text) {
    return std::as_bytes(std::span{text.data(), text.size()});
}

class Collector {
public:
    FrameDecoder::MessageSink sink() {
        return [this](std::string_view message) { messages.emplace_back(message); };
    }
    std::vector<std::string> messages;
};

TEST(FrameCodecTest, EncodesLengthAsLittleEndianPrefix) {
    const auto frame = encode_frame("abc");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame.value(), std::string("\x03\x00\x00\x00"
                                         "abc",
                                         7));
}

TEST(FrameCodecTest, EncodesEmptyMessageAsHeaderOnly) {
    const auto frame = encode_frame("");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame.value(), std::string(4, '\0'));
}

TEST(FrameCodecTest, RejectsMessageLargerThanLimit) {
    const std::string message(control_protocol::kMaxMessageBytes + 1, 'x');
    const auto frame = encode_frame(message);
    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error(), FrameError::kTooLarge);
}

TEST(FrameCodecTest, AcceptsMessageAtTheLimit) {
    const std::string message(control_protocol::kMaxMessageBytes, 'x');
    const auto frame = encode_frame(message);
    ASSERT_TRUE(frame.has_value());

    FrameDecoder decoder;
    Collector collector;
    ASSERT_TRUE(decoder.feed(as_bytes(frame.value()), collector.sink()).has_value());
    ASSERT_EQ(collector.messages.size(), 1U);
    EXPECT_EQ(collector.messages[0], message);
}

TEST(FrameCodecTest, DecodesSeveralFramesInOneChunk) {
    const std::string bytes = encode_frame("{\"a\":1}").value() + encode_frame("").value() +
                              encode_frame("{\"b\":2}").value();
    FrameDecoder decoder;
    Collector collector;
    ASSERT_TRUE(decoder.feed(as_bytes(bytes), collector.sink()).has_value());
    EXPECT_EQ(collector.messages, (std::vector<std::string>{"{\"a\":1}", "", "{\"b\":2}"}));
    EXPECT_EQ(decoder.buffered_bytes(), 0U);
}

TEST(FrameCodecTest, DecodesFrameSplitAtEveryByte) {
    const std::string bytes = encode_frame("{\"type\":\"hello\"}").value() +
                              encode_frame("{\"type\":\"meters\"}").value();
    FrameDecoder decoder;
    Collector collector;
    for (const char c : bytes) {
        const std::string one(1, c);
        ASSERT_TRUE(decoder.feed(as_bytes(one), collector.sink()).has_value());
    }
    EXPECT_EQ(collector.messages,
              (std::vector<std::string>{"{\"type\":\"hello\"}", "{\"type\":\"meters\"}"}));
    EXPECT_EQ(decoder.buffered_bytes(), 0U);
}

TEST(FrameCodecTest, KeepsPartialFrameUntilTheRestArrives) {
    const std::string frame = encode_frame("abcdef").value();
    FrameDecoder decoder;
    Collector collector;
    ASSERT_TRUE(decoder.feed(as_bytes(frame.substr(0, 6)), collector.sink()).has_value());
    EXPECT_TRUE(collector.messages.empty());
    EXPECT_EQ(decoder.buffered_bytes(), 6U);

    ASSERT_TRUE(decoder.feed(as_bytes(frame.substr(6)), collector.sink()).has_value());
    EXPECT_EQ(collector.messages, std::vector<std::string>{"abcdef"});
}

TEST(FrameCodecTest, RejectsOversizedLengthWithoutWaitingForTheBody) {
    // 本文が届く前に、長さだけで拒否する（巨大な入力をメモリに読み込まないため）。
    const auto too_large = static_cast<std::uint32_t>(control_protocol::kMaxMessageBytes + 1);
    std::string header(4, '\0');
    for (std::size_t i = 0; i < 4; ++i) {
        header[i] = static_cast<char>((too_large >> (8U * i)) & 0xFFU);
    }
    FrameDecoder decoder;
    Collector collector;
    const auto result = decoder.feed(as_bytes(header), collector.sink());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FrameError::kTooLarge);
    EXPECT_EQ(decoder.buffered_bytes(), 0U);
}

TEST(FrameCodecTest, StaysFailedAfterProtocolError) {
    const std::string bad("\xff\xff\xff\xff", 4);
    FrameDecoder decoder;
    Collector collector;
    ASSERT_FALSE(decoder.feed(as_bytes(bad), collector.sink()).has_value());

    // 境界を信用できないので、後から正しいフレームが届いても取り出さない。
    const auto again = decoder.feed(as_bytes(encode_frame("{}").value()), collector.sink());
    EXPECT_FALSE(again.has_value());
    EXPECT_TRUE(collector.messages.empty());
}

TEST(FrameCodecTest, DeliversFramesBeforeTheBadOne) {
    const std::string bytes = encode_frame("ok").value() + std::string("\xff\xff\xff\xff", 4);
    FrameDecoder decoder;
    Collector collector;
    EXPECT_FALSE(decoder.feed(as_bytes(bytes), collector.sink()).has_value());
    EXPECT_EQ(collector.messages, std::vector<std::string>{"ok"});
}

}  // namespace
}  // namespace vocroz::ipc

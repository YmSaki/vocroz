// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の MediaPlayerControl。呼ばれた操作を記録し、読み込みの失敗と、再生中の位置、
// ファイルの終わり、読み込みの失敗を再現できる。
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vocroz/session/ports/media_player_control.h"

namespace vocroz::session {

/// 呼ばれた操作を記録する MediaPlayerControl。
class FakeMediaPlayerControl final : public MediaPlayerControl {
public:
    common::Result<MediaInfo, control_protocol::MediaResult> load(
        const std::string& file, const std::string& target) override {
        loads_.push_back(file);
        target_ = target;
        playing_ = false;
        status_ = MediaPlayerStatus{};
        if (const auto it = errors_.find(file); it != errors_.end()) {
            loaded_ = false;
            return common::make_unexpected(it->second);
        }
        loaded_ = true;
        return info_;
    }

    void unload() override {
        ++unloads_;
        loaded_ = false;
        playing_ = false;
    }

    void play() override {
        ++plays_;
        playing_ = true;
    }

    void pause() override {
        ++pauses_;
        playing_ = false;
    }

    void seek(std::uint64_t frame) override {
        seeks_.push_back(frame);
        status_.position_frames = frame;
        status_.at_end = false;
    }

    void set_target(const std::string& target) override {
        target_ = target;
    }

    MediaPlayerStatus status() const override {
        return loaded_ ? status_ : MediaPlayerStatus{};
    }

    /// load() が返すファイルの情報。
    void set_info(MediaInfo info) {
        info_ = info;
    }
    /// そのファイルの load() を失敗させる。
    void fail_load(const std::string& file, control_protocol::MediaResult error) {
        errors_[file] = error;
    }
    /// 再生位置を進める。
    void set_position(std::uint64_t frame) {
        status_.position_frames = frame;
    }
    /// ファイルの終わりまで鳴らし終えたことにする。
    void reach_end() {
        status_.position_frames = info_.length_frames;
        status_.at_end = true;
    }
    /// 再生中の読み込みの失敗を再現する。
    void fail(audio_core::AudioFileError error) {
        status_.failure = error;
    }

    [[nodiscard]] const std::vector<std::string>& loads() const noexcept {
        return loads_;
    }
    [[nodiscard]] const std::vector<std::uint64_t>& seeks() const noexcept {
        return seeks_;
    }
    [[nodiscard]] const std::string& target() const noexcept {
        return target_;
    }
    [[nodiscard]] bool playing() const noexcept {
        return playing_;
    }
    [[nodiscard]] int plays() const noexcept {
        return plays_;
    }
    [[nodiscard]] int pauses() const noexcept {
        return pauses_;
    }
    [[nodiscard]] int unloads() const noexcept {
        return unloads_;
    }

private:
    std::vector<std::string> loads_;
    std::vector<std::uint64_t> seeks_;
    std::map<std::string, control_protocol::MediaResult> errors_;
    MediaInfo info_{2, 48'000 * 10};
    MediaPlayerStatus status_;
    std::string target_;
    bool loaded_ = false;
    bool playing_ = false;
    int plays_ = 0;
    int pauses_ = 0;
    int unloads_ = 0;
};

}  // namespace vocroz::session

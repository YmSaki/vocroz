// SPDX-License-Identifier: AGPL-3.0-only
//
// ChannelGroup の実装。

#include "vocroz/control_protocol/channel_group.h"

#include <algorithm>
#include <optional>

namespace vocroz::control_protocol {

ChannelGroup::~ChannelGroup() {
    const std::lock_guard lock{mutex_};
    for (MessageChannel* member : members_) {
        member->set_listener(nullptr);
    }
}

void ChannelGroup::add(MessageChannel& member) {
    const std::lock_guard lock{mutex_};
    if (std::find(members_.begin(), members_.end(), &member) != members_.end()) {
        return;
    }
    members_.push_back(&member);
    member.set_listener(listener_);
}

void ChannelGroup::remove(MessageChannel& member) {
    const std::lock_guard lock{mutex_};
    const auto it = std::find(members_.begin(), members_.end(), &member);
    if (it == members_.end()) {
        return;
    }
    members_.erase(it);
    member.set_listener(nullptr);
}

common::Result<void, ChannelError> ChannelGroup::send(std::string_view message) {
    const std::lock_guard lock{mutex_};
    std::optional<ChannelError> failure = ChannelError::kClosed;
    for (MessageChannel* member : members_) {
        const auto result = member->send(message);
        if (result) {
            failure.reset();
        } else if (failure) {
            failure = result.error();
        }
    }
    if (failure) {
        return common::make_unexpected(*failure);
    }
    return {};
}

void ChannelGroup::set_listener(MessageListener* listener) {
    const std::lock_guard lock{mutex_};
    listener_ = listener;
    for (MessageChannel* member : members_) {
        member->set_listener(listener);
    }
}

}  // namespace vocroz::control_protocol

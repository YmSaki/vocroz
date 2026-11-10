// SPDX-License-Identifier: AGPL-3.0-only
//
// external_output_book.h の実装。

#include "external_output_book.h"

#include "vocroz/session/entity_ids.h"

#include "detail/revision_utils.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
using cp::ErrorCode;
using cp::ExternalOutputState;

}  // namespace

ExternalOutputBook::ExternalOutputBook() {
    info_.entity_id = kExternalOutputEntityId;
}

void ExternalOutputBook::restore(const cp::ExternalOutputConfig& config,
                                 std::optional<transport::SrtpMasterKey> key) {
    info_.config = config;
    key_ = std::move(key);
    info_.key_configured = key_.has_value();
    info_.state = ExternalOutputState::kStopped;
    info_.last_result.reset();
}

common::Result<void, ErrorCode> ExternalOutputBook::set_config(
    const cp::ExternalOutputConfig& config) {
    if (info_.state != ExternalOutputState::kStopped) {
        return common::make_unexpected(ErrorCode::kInvalidState);
    }
    if (info_.config != config) {
        info_.config = config;
        // 宛先が変われば前の結果はもう当てはまらない。
        info_.last_result.reset();
        detail::bump_revision(info_.revision);
    }
    return {};
}

common::Result<void, ErrorCode> ExternalOutputBook::set_key(const transport::SrtpMasterKey& key) {
    if (info_.state != ExternalOutputState::kStopped) {
        return common::make_unexpected(ErrorCode::kInvalidState);
    }
    key_ = key;
    info_.key_configured = true;
    info_.last_result.reset();
    // 同じ鍵を貼り直しても revision を進める。鍵は UI に返さないので、UI からは同じかどうかを
    // 見分けられず、操作が受け付けられたことだけを示せばよい。
    detail::bump_revision(info_.revision);
    return {};
}

common::Result<ExternalOutputPlan, ErrorCode> ExternalOutputBook::plan() const {
    if (info_.config.host.empty() || !key_) {
        return common::make_unexpected(ErrorCode::kInvalidState);
    }
    ExternalOutputPlan plan;
    plan.host = info_.config.host;
    plan.port = info_.config.port;
    plan.source_id = info_.config.source_id;
    plan.channels = channels_of(info_.config.profile);
    plan.payload_type = info_.config.payload_type;
    plan.suite = to_transport_suite(info_.config.crypto_suite);
    return plan;
}

void ExternalOutputBook::record_start(
    const common::Result<void, cp::ExternalOutputResult>& result) {
    info_.state = result ? ExternalOutputState::kRunning : ExternalOutputState::kStopped;
    info_.last_result = result ? cp::ExternalOutputResult::kOk : result.error();
    detail::bump_revision(info_.revision);
}

void ExternalOutputBook::record_test_start(
    const common::Result<void, cp::ExternalOutputResult>& result) {
    if (result) {
        info_.state = ExternalOutputState::kTesting;
        info_.last_result.reset();
    } else {
        info_.state = ExternalOutputState::kStopped;
        info_.last_result = result.error();
    }
    detail::bump_revision(info_.revision);
}

void ExternalOutputBook::record_stop() {
    if (info_.state == ExternalOutputState::kStopped) {
        return;
    }
    info_.state = ExternalOutputState::kStopped;
    detail::bump_revision(info_.revision);
}

bool ExternalOutputBook::finish_test(cp::ExternalOutputResult result) {
    if (info_.state != ExternalOutputState::kTesting) {
        return false;
    }
    info_.state = ExternalOutputState::kStopped;
    info_.last_result = result;
    return true;
}

std::size_t channels_of(cp::ExternalOutputProfile profile) noexcept {
    switch (profile) {
        case cp::ExternalOutputProfile::kOpusStereo:
            return 2;
        case cp::ExternalOutputProfile::kOpusMono:
            return 1;
    }
    return 2;
}

transport::SrtpCryptoSuite to_transport_suite(cp::SrtpSuite suite) noexcept {
    switch (suite) {
        case cp::SrtpSuite::kAesCm128HmacSha1_80:
            return transport::SrtpCryptoSuite::kAesCm128HmacSha1_80;
        case cp::SrtpSuite::kAesCm128HmacSha1_32:
            return transport::SrtpCryptoSuite::kAesCm128HmacSha1_32;
    }
    return transport::SrtpCryptoSuite::kAesCm128HmacSha1_80;
}

}  // namespace vocroz::session

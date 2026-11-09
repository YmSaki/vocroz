// SPDX-License-Identifier: AGPL-3.0-only
//
// host_key_loader.h の実装。

#include "host_key_loader.h"

#include <cstdint>
#include <vector>

namespace vocroz::engine {
namespace {

using session::SecretError;
using session::SecretSlot;

// 平文の秘密鍵をゼロで上書きする。volatile を通して、使われないメモリへの書き込みとして
// 最適化で消されないようにする。
void wipe(std::vector<std::uint8_t>& bytes) noexcept {
    volatile std::uint8_t* data = bytes.data();
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        data[i] = 0;
    }
}

[[nodiscard]] common::Result<transport::StaticKeyPair, transport::TransportError> create_and_save(
    session::SecretStore& secrets, common::Logger& logger) {
    auto keys = transport::StaticKeyPair::generate();
    if (!keys) {
        return keys;
    }
    if (const auto saved = secrets.save(SecretSlot::kHostStaticKey, keys.value().private_key());
        !saved) {
        logger.log(common::LogLevel::kWarning,
                   "親機の鍵を保存できませんでした。次の起動では別の鍵になります");
    }
    return keys;
}

}  // namespace

common::Result<transport::StaticKeyPair, transport::TransportError> load_host_keys(
    session::SecretStore& secrets, common::Logger& logger) {
    auto loaded = secrets.load(SecretSlot::kHostStaticKey);
    if (loaded) {
        auto keys = transport::StaticKeyPair::from_private_key(loaded.value());
        wipe(loaded.value());
        if (keys || keys.error() != transport::TransportError::kInvalidKey) {
            return keys;
        }
        logger.log(common::LogLevel::kWarning, "保存した親機の鍵が不正なため、作り直します");
        return create_and_save(secrets, logger);
    }
    switch (loaded.error()) {
        case SecretError::kNotFound:
            logger.log(common::LogLevel::kInfo, "親機の鍵を新しく作ります");
            return create_and_save(secrets, logger);
        case SecretError::kCorrupted:
            logger.log(common::LogLevel::kWarning, "保存した親機の鍵を読めないため、作り直します");
            return create_and_save(secrets, logger);
        case SecretError::kIoError:
            // 一時的な失敗かもしれないので、保存した鍵を上書きしない。
            logger.log(common::LogLevel::kWarning,
                       "親機の鍵を読み込めないため、この起動だけの鍵を使います");
            return transport::StaticKeyPair::generate();
    }
    return transport::StaticKeyPair::generate();
}

}  // namespace vocroz::engine

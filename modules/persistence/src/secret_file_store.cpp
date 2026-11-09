// SPDX-License-Identifier: AGPL-3.0-only
//
// SecretFileStore の実装。

#include "secret_file_store.h"

#include <string_view>
#include <utility>

#include "atomic_file.h"

namespace vocroz::persistence {
namespace {

using session::SecretError;
using session::SecretSlot;

// ファイル名と、DataProtector に渡す用途の名前を兼ねる。用途を種類ごとに変えると、
// 親機の鍵のファイルを子機の鍵の名前に置き換えられても復号に失敗する。
std::string_view slot_name(SecretSlot slot) noexcept {
    switch (slot) {
        case SecretSlot::kHostStaticKey:
            return "host-static-key";
        case SecretSlot::kClientStaticKey:
            return "client-static-key";
        case SecretSlot::kExternalOutputKey:
            return "external-output-key";
    }
    return "unknown";
}

AtomicFile secret_file(const std::filesystem::path& directory, SecretSlot slot) {
    return AtomicFile{directory / (std::string{slot_name(slot)} + ".bin")};
}

}  // namespace

SecretFileStore::SecretFileStore(std::filesystem::path directory,
                                 std::unique_ptr<DataProtector> protector)
    : directory_(std::move(directory)), protector_(std::move(protector)) {}

common::Result<std::vector<std::uint8_t>, SecretError> SecretFileStore::load(SecretSlot slot) {
    const auto sealed = secret_file(directory_, slot).read_current();
    if (!sealed) {
        return common::make_unexpected(sealed.error() == AtomicFileError::kTooLarge
                                           ? SecretError::kCorrupted
                                           : SecretError::kIoError);
    }
    if (!sealed.value()) {
        return common::make_unexpected(SecretError::kNotFound);
    }
    auto plain = protector_->unprotect(*sealed.value(), slot_name(slot));
    if (!plain) {
        return common::make_unexpected(SecretError::kCorrupted);
    }
    return std::move(*plain);
}

common::Result<void, SecretError> SecretFileStore::save(SecretSlot slot,
                                                        std::span<const std::uint8_t> value) {
    const auto sealed = protector_->protect(value, slot_name(slot));
    if (!sealed) {
        return common::make_unexpected(SecretError::kIoError);
    }
    auto file = secret_file(directory_, slot);
    if (!file.commit(*sealed, /*keep_current_as_backup=*/false)) {
        return common::make_unexpected(SecretError::kIoError);
    }
    return {};
}

common::Result<void, SecretError> SecretFileStore::erase(SecretSlot slot) {
    auto file = secret_file(directory_, slot);
    if (!file.remove_all()) {
        return common::make_unexpected(SecretError::kIoError);
    }
    return {};
}

}  // namespace vocroz::persistence

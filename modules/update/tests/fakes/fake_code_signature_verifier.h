// SPDX-License-Identifier: AGPL-3.0-only
//
// CodeSignatureVerifier のフェイク。結果と呼び出し回数をテストが決め、確かめる。
#pragma once

#include <string>

#include "vocroz/update/ports/code_signature_verifier.h"

namespace vocroz::update::fakes {

class FakeCodeSignatureVerifier final : public ports::CodeSignatureVerifier {
public:
    bool verify(const SemanticVersion& /*version*/, std::string_view publisher) override {
        ++calls;
        last_publisher = std::string{publisher};
        return trusted;
    }

    bool trusted = true;
    int calls = 0;
    std::string last_publisher;
};

}  // namespace vocroz::update::fakes

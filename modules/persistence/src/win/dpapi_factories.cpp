// SPDX-License-Identifier: AGPL-3.0-only
//
// Windows だけのアダプターのファクトリー関数の実装。

#include <memory>

#include "vocroz/persistence/factories.h"

#include "persistence_constants.h"
#include "secret_file_store.h"
#include "win/dpapi_data_protector.h"

namespace vocroz::persistence {

std::unique_ptr<session::SecretStore> make_dpapi_secret_store(
    const std::filesystem::path& data_dir) {
    return std::make_unique<SecretFileStore>(data_dir / kSecretsDirectoryName,
                                             std::make_unique<DpapiDataProtector>());
}

}  // namespace vocroz::persistence

// SPDX-License-Identifier: AGPL-3.0-only
//
// mmcss_registration.h の実装。

#include "win/mmcss_registration.h"

#include <avrt.h>

namespace vocroz::device::win {

MmcssRegistration::MmcssRegistration() noexcept {
    DWORD task_index = 0;
    handle_ = ::AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
}

MmcssRegistration::~MmcssRegistration() {
    if (handle_ != nullptr) {
        ::AvRevertMmThreadCharacteristics(handle_);
    }
}

}  // namespace vocroz::device::win

#pragma once

#include "hle/rt64_rsp.h"
#include "shared/rt64_rsp_viewport.h"
#include "common/rt64_common.h"
#include <cmath>

namespace dkr::runtime::presentation {

inline void fill_postrace_scissor(RT64::FixedRect& scissor) {
    scissor = RT64::FixedRect(0, 0, 320 * 4, 240 * 4);
}

inline void fill_postrace_viewport(interop::RSPViewport& viewport) {
    // Preserve mirrored-course handedness and depth range. Only the native
    // presentation rectangle changes; RT64 AUTO supplies the host aspect.
    viewport.scale.x = std::copysign(160.F, viewport.scale.x);
    viewport.scale.y = std::copysign(120.F, viewport.scale.y);
    viewport.translate.x = 160.F;
    viewport.translate.y = 120.F;
}

} // namespace dkr::runtime::presentation

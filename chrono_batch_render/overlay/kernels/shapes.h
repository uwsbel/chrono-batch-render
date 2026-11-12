// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"

namespace chrono::batch_render {

static const uint kOverlaySphereSegments = 24;
static const uint kOverlayCircleSegments = 32;
static const uint kOverlayArrowHeadLines = 6;
static const uint kOverlayCapsuleSegments = 16;
static const uint kOverlayCapsuleArcSegments = 8;
static const uint kOverlayFrustumLines = 15;
static const uint kOverlayLightLines = 3 * kOverlaySphereSegments;

}

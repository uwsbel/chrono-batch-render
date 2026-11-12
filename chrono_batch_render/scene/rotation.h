// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/export.h"
#include "chrono_batch_render/stage/schema/types.h"

namespace chrono::batch_render {

CHRONO_BATCH_RENDER_API Quatd LookAlong(Double3 direction, Double3 up = {0, 0, 1});
CHRONO_BATCH_RENDER_API Quatd LookAt(Double3 eye, Double3 target, Double3 up = {0, 0, 1});
CHRONO_BATCH_RENDER_API Quatd FromAxisAngle(Double3 axis, double degrees);
CHRONO_BATCH_RENDER_API Quatd FromAzimuthElevation(double azimuth, double elevation);
CHRONO_BATCH_RENDER_API Quatd FromAxes(Double3 x, Double3 y, Double3 z);
CHRONO_BATCH_RENDER_API Quatd FromEulerDegrees(Double3 degrees);
CHRONO_BATCH_RENDER_API Double3 ToEulerDegrees(Quatd rotation);

CHRONO_BATCH_RENDER_API Quatd Multiply(Quatd a, Quatd b);
CHRONO_BATCH_RENDER_API Quatd Conjugate(Quatd rotation);
CHRONO_BATCH_RENDER_API Quatd Normalize(Quatd rotation);
CHRONO_BATCH_RENDER_API Double3 Rotate(Quatd rotation, Double3 vector);

}

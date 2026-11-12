// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/scene/records/bounds.h"

namespace chrono::batch_render {

#ifdef __SLANG__

groupshared uint s_extents[6];
groupshared uint s_job;

void BeginExtents(uint thread, uint job) {
    if (thread == 0) {
        s_job = job;
        for (uint axis = 0; axis < 3; ++axis) {
            s_extents[axis] = ~0u;
            s_extents[3 + axis] = 0;
        }
    }
    GroupMemoryBarrierWithGroupSync();
}

void Extend(Ptr<uint> extents, uint jobCount, uint job, float3 lower, float3 upper) {
    for (uint axis = 0; axis < 3; ++axis) {
        const uint low = OrderedFloat(lower[axis]);
        const uint high = OrderedFloat(upper[axis]);
        if (job == s_job) {
            InterlockedMin(s_extents[axis], low);
            InterlockedMax(s_extents[3 + axis], high);
        } else {
            InterlockedMin(extents[job * 3 + axis], low);
            InterlockedMax(extents[(jobCount + job) * 3 + axis], high);
        }
    }
}

void EndExtents(Ptr<uint> extents, uint jobCount, uint thread, bool live) {
    GroupMemoryBarrierWithGroupSync();
    if (thread < 6 && live) {
        const uint axis = thread % 3;
        if (thread < 3) {
            InterlockedMin(extents[s_job * 3 + axis], s_extents[thread]);
        } else {
            InterlockedMax(extents[(jobCount + s_job) * 3 + axis], s_extents[thread]);
        }
    }
}

void FinishExtents(Ptr<uint> extents, uint jobCount, uint job, Ptr<Float3> bounds) {
    float3 lower = float3(0);
    float3 upper = float3(0);
    if (extents[job * 3] != ~0u) {
        for (uint axis = 0; axis < 3; ++axis) {
            lower[axis] = FloatFromOrdered(extents[job * 3 + axis]);
            upper[axis] = FloatFromOrdered(extents[(jobCount + job) * 3 + axis]);
        }
    }
    bounds[0] = Float3(lower);
    bounds[1] = Float3(upper);
}

#endif

}

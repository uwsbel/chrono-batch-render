// SPDX-License-Identifier: BSD-3-Clause

import chrono_batch_render.device.kernels.device;
__exported import chrono_batch_render.render.kernels.material.primitives.diffuse;

struct PhotometricAngles {
    float mu0;
    float mu;
    float cosG;
    float cosPsi;

    __init(float3 outgoing, float3 incoming) {
        mu0 = incoming.z;
        mu = max(outgoing.z, 1e-3f);
        cosG = dot(incoming, outgoing);
        const float lengths = length(incoming.xy) * length(outgoing.xy);
        cosPsi = lengths > 1e-10f ? dot(incoming.xy, outgoing.xy) / lengths : 1;
    }

    float Phase() {
        return acos(clamp(cosG, -1.0f, 1.0f));
    }
}

float HenyeyGreenstein(float xi, float cosG) {
    const float xi2 = xi * xi;
    return (1 - xi2) / pow(max(1 + 2 * xi * cosG + xi2, 1e-6f), 1.5f);
}

float DoubleHenyeyGreenstein(float b, float c, float cosG) {
    const float b2 = b * b;
    const float back = (1 - b2) / pow(max(1 - 2 * b * cosG + b2, 1e-6f), 1.5f);
    const float forward = (1 - b2) / pow(max(1 + 2 * b * cosG + b2, 1e-6f), 1.5f);
    return 0.5f * (1 + c) * back + 0.5f * (1 - c) * forward;
}

float PhotometricPdf(float3 incoming) {
    return max(incoming.z, 0.0f) / kPi;
}

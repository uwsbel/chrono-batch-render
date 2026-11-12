// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.render.kernels.shapes.ishape;
import chrono_batch_render.device.kernels.device;

struct RoundConeHit {
    ShapeHit hit;
    float along;
}

struct RoundConeRoots {
    float t;
    float3 normal;
    bool found;

    [mutating]
    void Offer(float candidate, float3 candidateNormal, float tMin, bool valid) {
        if (valid && candidate >= tMin && (!found || candidate < t)) {
            t = candidate;
            normal = candidateNormal;
            found = true;
        }
    }
}

void OfferCap(inout RoundConeRoots roots, float3 origin, float3 direction, float tMin, float3 center,
              float radius, float3 axis, float slope, bool start) {
    const float3 offset = origin - center;
    const float b = dot(offset, direction);
    const float c = dot(offset, offset) - radius * radius;
    const float h = b * b - c;
    if (h < 0 || radius <= 0) {
        return;
    }
    const float root = sqrt(h);
    for (int side = -1; side <= 1; side += 2) {
        const float t = -b + side * root;
        const float3 normal = (offset + t * direction) / radius;
        const float tilt = dot(normal, axis);
        roots.Offer(t, normal, tMin, start ? tilt <= slope : tilt >= slope);
    }
}

RoundConeHit IntersectRoundCone(float3 a, float ra, float3 b, float rb, float3 rayOrigin,
                                float3 rayDirection, float rayTMin) {
    RoundConeHit result;
    result.hit.t = kInfinity;
    result.hit.normal = float3(0, 0, 1);
    result.hit.inside = false;
    result.along = 0;
    const float scale = length(rayDirection);
    if (!(scale > 0)) {
        return result;
    }
    const float3 middle = (a + b) / 2;
    const float3 direction = rayDirection / scale;
    float3 origin = rayOrigin - middle;
    const float closest = -dot(origin, direction);
    origin += closest * direction;
    const float tMin = rayTMin * scale - closest;
    const float3 pa = a - middle;
    const float3 pb = b - middle;

    const float3 ba = pb - pa;
    const float m0 = dot(ba, ba);
    const float rr = ra - rb;
    RoundConeRoots roots;
    roots.t = kInfinity;
    roots.normal = float3(0, 0, 1);
    roots.found = false;
    if (m0 <= rr * rr) {
        const bool first = ra >= rb;
        OfferCap(roots, origin, direction, tMin, first ? pa : pb, first ? ra : rb, float3(0), 1,
                 true);
    } else {
        const float span = sqrt(m0);
        const float3 axis = ba / span;
        const float slope = rr / span;
        const float3 oa = origin - pa;
        const float m1 = dot(ba, oa);
        const float m2 = dot(ba, direction);
        const float m3 = dot(direction, oa);
        const float m5 = dot(oa, oa);
        const float d2 = m0 - rr * rr;
        const float k2 = d2 - m2 * m2;
        const float k1 = d2 * m3 - m1 * m2 + m2 * rr * ra;
        const float k0 = d2 * m5 - m1 * m1 + m1 * rr * ra * 2 - m0 * ra * ra;
        const float h = k1 * k1 - k0 * k2;
        if (h >= 0 && k2 != 0) {
            const float root = sqrt(h);
            const float cosine = sqrt(max(1 - slope * slope, 0.0f));
            for (int side = -1; side <= 1; side += 2) {
                const float t = (-k1 + side * root) / k2;
                const float y = m1 - ra * rr + t * m2;
                const float3 q = oa + t * direction;
                const float3 radial = q - dot(q, axis) * axis;
                const float spread = length(radial);
                const float3 normal =
                    spread > 0 ? radial / spread * cosine + axis * slope : axis;
                roots.Offer(t, normal, tMin, y > 0 && y < d2);
            }
        }
        OfferCap(roots, origin, direction, tMin, pa, ra, axis, slope, true);
        OfferCap(roots, origin, direction, tMin, pb, rb, axis, slope, false);
    }
    if (!roots.found) {
        return result;
    }
    const float3 at = origin + roots.t * direction;
    result.hit.t = (roots.t + closest) / scale;
    result.hit.normal = roots.normal;
    result.hit.inside = true;
    result.along = m0 > 0 ? saturate(dot(at - pa, ba) / m0) : 0;
    return result;
}

RoundConeHit IntersectRibbon(float3 a, float ra, float3 b, float rb, float3 normal,
                             float3 rayOrigin, float3 rayDirection, float rayTMin) {
    RoundConeHit result;
    result.hit.t = kInfinity;
    result.hit.normal = normal;
    result.hit.inside = false;
    result.along = 0;
    const float scale = length(rayDirection);
    if (!(scale > 0)) {
        return result;
    }
    const float3 middle = (a + b) / 2;
    const float3 direction = rayDirection / scale;
    float3 origin = rayOrigin - middle;
    const float closest = -dot(origin, direction);
    origin += closest * direction;
    const float tMin = rayTMin * scale - closest;
    const float facing = dot(direction, normal);
    if (facing == 0) {
        return result;
    }
    const float3 pa = a - middle;
    const float t = dot(pa - origin, normal) / facing;
    if (!(t >= tMin)) {
        return result;
    }
    const float3 at = origin + t * direction;
    const float3 ba = b - a;
    const float m0 = dot(ba, ba);
    const float s = m0 > 0 ? dot(at - pa, ba) / m0 : 0;
    if (s < 0 || s > 1) {
        return result;
    }
    const float3 radial = at - (pa + s * ba);
    const float radius = lerp(ra, rb, s);
    if (dot(radial, radial) > radius * radius) {
        return result;
    }
    result.hit.t = (t + closest) / scale;
    result.hit.inside = true;
    result.along = s;
    return result;
}

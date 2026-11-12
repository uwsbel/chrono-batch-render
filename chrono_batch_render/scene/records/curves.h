// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/stage/schema/ids.h"
#include "chrono_batch_render/stage/schema/schema.h"
#include "chrono_batch_render/stage/schema/types.h"

namespace chrono::batch_render {

static const float kDefaultCurveWidth = 1.0f;
static const float kDefaultCurveTolerance = 0.05f;
static const uint kDefaultMaxCurveSteps = 16;
static const uint kMostCurveSteps = 255;
static const float kCurveSizeTolerance = 0.05f;

struct CurveSegment {
    Float4 start;
    Float4 end;
    uint curve;
    uint span;
    uint bits;
    uint normal;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(CurveSegment, 48);

static const uint kCurveRibbon = 1u << 22;
static const uint kCurveExact = 1u << 26;

static const uint kNoCurve = 0xFFFFFFFFu;

static const uint kCurveKindBasis = 0;
static const uint kCurveKindHermite = 1;
static const uint kCurveKindNurbs = 2;

struct CurvesRecord {
    uint64_t blas;
    uint64_t points;
    uint64_t widths;
    uint64_t counts;
    uint64_t starts;
    uint64_t varyingStarts;
    uint64_t spanFirst;
    uint64_t spanSteps;
    uint64_t segments;
    uint64_t normals;
    uint64_t tangents;
    uint64_t orders;
    uint64_t knots;
    uint64_t knotStarts;
    uint64_t pointWeights;
    uint pointCount;
    uint widthCount;
    uint curveCount;
    uint segmentCount;
    uint normalCount;
    uint kind;
    uint tangentCount;
    uint knotCount;
    uint weightCount;
    uint padding;
    ArrayRef bounds;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(CurvesRecord, 176);

#ifdef __SLANG__

uint CurveBits(uint steps, uint step, CurveType type, CurveBasis basis, CurveWrap wrap) {
    return steps | (step << 8) | (uint(type) << 16) | (uint(basis) << 18) | (uint(wrap) << 20);
}

uint BitsSteps(uint bits) {
    return bits & 0xFFu;
}

uint BitsStep(uint bits) {
    return (bits >> 8) & 0xFFu;
}

CurveType BitsType(uint bits) {
    return CurveType((bits >> 16) & 3u);
}

CurveBasis BitsBasis(uint bits) {
    return CurveBasis((bits >> 18) & 3u);
}

CurveWrap BitsWrap(uint bits) {
    return CurveWrap((bits >> 20) & 3u);
}

bool BitsRibbon(uint bits) {
    return (bits & kCurveRibbon) != 0;
}

bool BitsExact(uint bits) {
    return (bits & kCurveExact) != 0;
}

Interpolation BitsWidths(uint bits) {
    return Interpolation((bits >> 23) & 7u);
}

uint PackNormal(float3 normal) {
    float2 xy = normal.xy / (abs(normal.x) + abs(normal.y) + abs(normal.z));
    if (normal.z < 0) {
        xy = (1 - abs(xy.yx)) * float2(xy.x >= 0 ? 1 : -1, xy.y >= 0 ? 1 : -1);
    }
    const uint2 words = uint2(round(saturate(xy * 0.5f + 0.5f) * 65535));
    return words.x | (words.y << 16);
}

float3 UnpackNormal(uint packed) {
    const float2 xy = float2(packed & 0xFFFFu, packed >> 16) / 65535 * 2 - 1;
    float3 normal = float3(xy, 1 - abs(xy.x) - abs(xy.y));
    const float fold = saturate(-normal.z);
    normal.x += normal.x >= 0 ? -fold : fold;
    normal.y += normal.y >= 0 ? -fold : fold;
    return normalize(normal);
}

uint CurvePointCount(CurvesRecord record, uint curve) {
    const uint first = Ptr<uint>(record.starts)[curve];
    const uint count = uint(max(Ptr<int>(record.counts)[curve], 0));
    return first < record.pointCount ? min(first + count, record.pointCount) - first : 0;
}

uint CurveVaryingCount(uint count, CurveType type, CurveBasis basis, CurveWrap wrap) {
    if (type == CurveType::Linear) {
        return count;
    }
    if (basis == CurveBasis::Bezier) {
        if (wrap == CurveWrap::Periodic) {
            return count / 3;
        }
        return count >= 4 ? (count - 1) / 3 + 1 : 0;
    }
    if (wrap == CurveWrap::Periodic || wrap == CurveWrap::Pinned) {
        return count;
    }
    return count >= 4 ? count - 2 : 0;
}

uint FindCurve(CurvesRecord record, uint point) {
    const Ptr<uint> starts = Ptr<uint>(record.starts);
    uint low = 0;
    uint high = record.curveCount - 1;
    while (low < high) {
        const uint middle = (low + high + 1) / 2;
        if (starts[middle] <= point) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

struct CurveSpan {
    uint curve;
    uint4 controls;
    bool cubic;
    CurveBasis basis;
    bool startPhantom;
    bool endPhantom;
    uint2 ends;
    uint kind;
    uint degree;
    float knots[6];
    float4 rational;

    float4 NurbsWeights(float t) {
        const uint p = degree;
        const float u = lerp(knots[p - 1], knots[p], t);
        float n[4] = {1, 0, 0, 0};
        float left[4] = {0, 0, 0, 0};
        float right[4] = {0, 0, 0, 0};
        for (uint j = 1; j <= p; ++j) {
            left[j] = u - knots[p - j];
            right[j] = knots[p - 1 + j] - u;
            float saved = 0;
            for (uint r = 0; r < j; ++r) {
                const float under = right[r + 1] + left[j - r];
                const float temp = under != 0 ? n[r] / under : 0;
                n[r] = saved + right[r + 1] * temp;
                saved = left[j - r] * temp;
            }
            n[j] = saved;
        }
        float4 w = float4(n[0], n[1], n[2], n[3]) * rational;
        const float sum = w.x + w.y + w.z + w.w;
        return sum != 0 ? w / sum : float4(1, 0, 0, 0);
    }

    float4 BasisWeights(float t) {
        if (kind == kCurveKindNurbs) {
            return NurbsWeights(t);
        }
        if (!cubic || kind == kCurveKindHermite) {
            return float4(1 - t, t, 0, 0);
        }
        const float t2 = t * t;
        const float t3 = t2 * t;
        float4 w;
        if (basis == CurveBasis::Bezier) {
            const float s = 1 - t;
            w = float4(s * s * s, 3 * t * s * s, 3 * t2 * s, t3);
        } else if (basis == CurveBasis::Bspline) {
            const float s = 1 - t;
            w = float4(s * s * s, 3 * t3 - 6 * t2 + 4, -3 * t3 + 3 * t2 + 3 * t + 1, t3) / 6;
        } else {
            w = 0.5f * float4(-t3 + 2 * t2 - t, 3 * t3 - 5 * t2 + 2, -3 * t3 + 4 * t2 + t, t3 - t2);
        }
        return w;
    }

    float4 Weights(float t) {
        float4 w = BasisWeights(t);
        if (startPhantom) {
            w.y += 2 * w.x;
            w.z -= w.x;
            w.x = 0;
        }
        if (endPhantom) {
            w.z += 2 * w.w;
            w.y -= w.w;
            w.w = 0;
        }
        return w;
    }

    float3 Position(Ptr<Float3> points, float t) {
        return Position(points, points, t);
    }

    float3 Position(Ptr<Float3> points, Ptr<Float3> tangents, float t) {
        if (kind == kCurveKindHermite) {
            const float t2 = t * t;
            const float t3 = t2 * t;
            return (2 * t3 - 3 * t2 + 1) * points[controls.x].Vector() +
                   (t3 - 2 * t2 + t) * tangents[controls.x].Vector() +
                   (-2 * t3 + 3 * t2) * points[controls.y].Vector() +
                   (t3 - t2) * tangents[controls.y].Vector();
        }
        const float4 w = Weights(t);
        return w.x * points[controls.x].Vector() + w.y * points[controls.y].Vector() +
               w.z * points[controls.z].Vector() + w.w * points[controls.w].Vector();
    }

    void Bezier(Ptr<Float3> points, out float3 b[4]) {
        float3 p[4];
        p[0] = points[controls.x].Vector();
        p[1] = points[controls.y].Vector();
        p[2] = points[controls.z].Vector();
        p[3] = points[controls.w].Vector();
        if (startPhantom) {
            p[0] = 2 * p[1] - p[2];
        }
        if (endPhantom) {
            p[3] = 2 * p[2] - p[1];
        }
        if (!cubic) {
            b[0] = p[0];
            b[1] = lerp(p[0], p[1], 1.0f / 3);
            b[2] = lerp(p[0], p[1], 2.0f / 3);
            b[3] = p[1];
        } else if (basis == CurveBasis::Bezier) {
            b = p;
        } else if (basis == CurveBasis::Bspline) {
            b[0] = (p[0] + 4 * p[1] + p[2]) / 6;
            b[1] = (2 * p[1] + p[2]) / 3;
            b[2] = (p[1] + 2 * p[2]) / 3;
            b[3] = (p[1] + 4 * p[2] + p[3]) / 6;
        } else {
            b[0] = p[1];
            b[1] = p[1] + (p[2] - p[0]) / 6;
            b[2] = p[2] - (p[3] - p[1]) / 6;
            b[3] = p[2];
        }
    }

}

uint CurveSpans(uint count, CurveType type, CurveBasis basis, CurveWrap wrap) {

    const bool periodic = wrap == CurveWrap::Periodic;
    if (type == CurveType::Linear) {
        return periodic ? (count > 2 ? count : 0) : (count > 0 ? count - 1 : 0);
    }
    if (basis == CurveBasis::Bezier) {
        return periodic ? (count % 3 == 0 ? count / 3 : 0) : (count >= 4 ? (count - 1) / 3 : 0);
    }
    if (wrap == CurveWrap::Pinned) {
        return count >= 2 ? count - 1 : 0;
    }
    return periodic ? (count >= 3 ? count : 0) : (count >= 4 ? count - 3 : 0);
}

void ClearCurveSpan(uint point, out CurveSpan span) {
    span.curve = kNoCurve;
    span.controls = uint4(point);
    span.cubic = false;
    span.basis = CurveBasis::Bezier;
    span.startPhantom = false;
    span.endPhantom = false;
    span.ends = uint2(0);
    span.kind = kCurveKindBasis;
    span.degree = 1;
    for (uint i = 0; i < 6; ++i) {
        span.knots[i] = 0;
    }
    span.rational = float4(1);
}

bool MakeCurveSpan(uint first, uint count, uint varying, uint curve, uint point, CurveType type,
                   CurveBasis basis, CurveWrap wrap, out CurveSpan span) {
    ClearCurveSpan(point, span);
    span.cubic = type == CurveType::Cubic;
    span.basis = basis;
    const uint i = point - first;
    if (point < first || i >= count) {
        return false;
    }
    const bool periodic = wrap == CurveWrap::Periodic;
    uint k = i;
    uint spans = 0;
    if (!span.cubic) {
        spans = periodic ? (count > 2 ? count : 0) : count - 1;
        span.controls = uint4(point, first + (i + 1) % count, 0, 0).xyyy;
    } else if (basis == CurveBasis::Bezier) {
        if (i % 3 != 0) {
            return false;
        }
        k = i / 3;
        spans = periodic ? (count % 3 == 0 ? count / 3 : 0) : (count >= 4 ? (count - 1) / 3 : 0);
        span.controls =
            uint4(point, first + (i + 1) % count, first + (i + 2) % count, first + (i + 3) % count);
    } else if (wrap == CurveWrap::Pinned) {
        spans = count >= 2 ? count - 1 : 0;
        span.startPhantom = i == 0;
        span.endPhantom = i + 2 >= count;
        span.controls = uint4(span.startPhantom ? point : point - 1, point, point + 1,
                              span.endPhantom ? point + 1 : point + 2);
    } else {
        spans = periodic ? (count >= 3 ? count : 0) : (count >= 4 ? count - 3 : 0);
        span.controls =
            uint4(point, first + (i + 1) % count, first + (i + 2) % count, first + (i + 3) % count);
    }
    if (k >= spans) {
        return false;
    }
    const uint values = CurveVaryingCount(count, type, basis, wrap);
    span.curve = curve;
    span.ends = uint2(varying + k, varying + (values > 0 ? (k + 1) % values : 0));
    return true;
}

uint CurveKindSpans(uint kind, uint count, uint order, CurveType type, CurveBasis basis,
                    CurveWrap wrap) {
    if (kind == kCurveKindHermite) {
        return count >= 2 ? count - 1 : 0;
    }
    if (kind == kCurveKindNurbs) {
        return order >= 2 && order <= 4 && count >= order ? count - order + 1 : 0;
    }
    return CurveSpans(count, type, basis, wrap);
}

uint CurveKindVaryingCount(uint kind, uint count, uint order, CurveType type, CurveBasis basis,
                           CurveWrap wrap) {
    if (kind == kCurveKindHermite) {
        return count;
    }
    if (kind == kCurveKindNurbs) {
        const uint spans = CurveKindSpans(kind, count, order, type, basis, wrap);
        return spans > 0 ? spans + 1 : 0;
    }
    return CurveVaryingCount(count, type, basis, wrap);
}

bool MakeHermiteSpan(CurvesRecord record, uint curve, uint point, out CurveSpan span) {
    ClearCurveSpan(point, span);
    const uint first = Ptr<uint>(record.starts)[curve];
    const uint count = CurvePointCount(record, curve);
    const uint i = point - first;
    if (point < first || i + 1 >= count || point + 1 >= record.tangentCount) {
        return false;
    }
    const uint varying = Ptr<uint>(record.varyingStarts)[curve];
    span.kind = kCurveKindHermite;
    span.cubic = true;
    span.curve = curve;
    span.controls = uint4(point, point + 1, point + 1, point + 1);
    span.ends = uint2(varying + i, varying + i + 1);
    return true;
}

bool MakeNurbsSpan(CurvesRecord record, uint curve, uint point, out CurveSpan span) {
    ClearCurveSpan(point, span);
    if (record.orders == 0 || record.knots == 0) {
        return false;
    }
    const uint first = Ptr<uint>(record.starts)[curve];
    const uint count = CurvePointCount(record, curve);
    const uint order = uint(max(Ptr<int>(record.orders)[curve], 0));
    const uint spans = CurveKindSpans(kCurveKindNurbs, count, order, CurveType::Cubic,
                                      CurveBasis::Bezier, CurveWrap::Nonperiodic);
    const uint i = point - first;
    if (point < first || i >= spans) {
        return false;
    }
    const uint knotStart = Ptr<uint>(record.knotStarts)[curve];
    if (knotStart + count + order > record.knotCount) {
        return false;
    }
    const uint p = order - 1;
    const Ptr<float> knots = Ptr<float>(record.knots);
    for (uint j = 0; j < 2 * p; ++j) {
        span.knots[j] = knots[knotStart + i + 1 + j];
    }
    if (!(span.knots[p] > span.knots[p - 1])) {
        return false;
    }
    for (uint j = 0; j <= p; ++j) {
        span.rational[j] = record.weightCount == record.pointCount
                               ? Ptr<float>(record.pointWeights)[point + j]
                               : 1;
    }
    const uint varying = Ptr<uint>(record.varyingStarts)[curve];
    span.kind = kCurveKindNurbs;
    span.degree = p;
    span.cubic = p > 1;
    span.curve = curve;
    span.controls = uint4(point, point + min(1u, p), point + min(2u, p), point + min(3u, p));
    for (uint j = p + 1; j < 4; ++j) {
        span.rational[j] = 0;
    }
    span.ends = uint2(varying + i, varying + i + 1);
    return true;
}

bool FindCurveSpanIn(CurvesRecord record, uint curve, uint point, CurveType type, CurveBasis basis,
                     CurveWrap wrap, out CurveSpan span) {
    if (point >= record.pointCount || curve >= record.curveCount) {
        return MakeCurveSpan(0, 0, 0, curve, point, type, basis, wrap, span);
    }
    if (record.kind == kCurveKindHermite) {
        return MakeHermiteSpan(record, curve, point, span);
    }
    if (record.kind == kCurveKindNurbs) {
        return MakeNurbsSpan(record, curve, point, span);
    }
    return MakeCurveSpan(Ptr<uint>(record.starts)[curve], CurvePointCount(record, curve),
                         Ptr<uint>(record.varyingStarts)[curve], curve, point, type, basis, wrap,
                         span);
}

bool FindCurveSpan(CurvesRecord record, uint point, CurveType type, CurveBasis basis,
                   CurveWrap wrap, out CurveSpan span) {
    const uint curve =
        point < record.pointCount && record.curveCount > 0 ? FindCurve(record, point) : 0;
    return FindCurveSpanIn(record, curve, point, type, basis, wrap, span);
}

float CurveSpanRadius(CurvesRecord record, Interpolation interpolation, CurveSpan span, float t) {
    if (record.widthCount == 0) {
        return kDefaultCurveWidth / 2;
    }
    const Ptr<float> widths = Ptr<float>(record.widths);
    const uint last = record.widthCount - 1;
    float width = widths[0];
    if (interpolation == Interpolation::Uniform) {
        width = widths[min(span.curve, last)];
    } else if (interpolation == Interpolation::Vertex) {
        const float4 w = span.Weights(t);
        const uint4 at = min(span.controls, uint4(last));
        width = w.x * widths[at.x] + w.y * widths[at.y] + w.z * widths[at.z] + w.w * widths[at.w];
    } else if (interpolation != Interpolation::Constant) {
        width = lerp(widths[min(span.ends.x, last)], widths[min(span.ends.y, last)], t);
    }
    return abs(width) / 2;
}

float3 CurveSpanVector(Ptr<Float3> values, uint count, Interpolation interpolation, CurveSpan span,
                       float t) {
    if (count == 0) {
        return float3(0);
    }
    const uint last = count - 1;
    if (interpolation == Interpolation::Uniform) {
        return values[min(span.curve, last)].Vector();
    }
    if (interpolation == Interpolation::Vertex) {
        const float4 w = span.Weights(t);
        const uint4 at = min(span.controls, uint4(last));
        return w.x * values[at.x].Vector() + w.y * values[at.y].Vector() +
               w.z * values[at.z].Vector() + w.w * values[at.w].Vector();
    }
    if (interpolation != Interpolation::Constant) {
        return lerp(values[min(span.ends.x, last)].Vector(),
                    values[min(span.ends.y, last)].Vector(), t);
    }
    return values[0].Vector();
}

static const uint kPathSamples = 16;

float CurveSpanLength(Ptr<Float3> points, CurveSpan span, float until) {
    float total = 0;
    float3 last = span.Position(points, 0);
    for (uint step = 1; step <= kPathSamples; ++step) {
        const float t = until * float(step) / float(kPathSamples);
        const float3 at = span.Position(points, t);
        total += distance(at, last);
        last = at;
    }
    return total;
}

float CurveSpanParameter(Ptr<Float3> points, CurveSpan span, float along) {
    float travelled = 0;
    float3 last = span.Position(points, 0);
    for (uint step = 1; step <= kPathSamples; ++step) {
        const float t = float(step) / float(kPathSamples);
        const float3 at = span.Position(points, t);
        const float chord = distance(at, last);
        if (travelled + chord >= along) {
            const float share = chord > 0 ? (along - travelled) / chord : 0;
            return (float(step - 1) + saturate(share)) / float(kPathSamples);
        }
        travelled += chord;
        last = at;
    }
    return 1;
}

bool CurvePathPoint(Ptr<Float3> points, uint pointCount, Ptr<int> counts, uint curveCount,
                    uint curve, CurveType type, CurveBasis basis, CurveWrap wrap, bool byDistance,
                    float position, out float3 at, out float3 tangent, out float placed) {
    at = float3(0);
    tangent = float3(0);
    placed = position;
    if (curve >= curveCount) {
        return false;
    }
    uint first = 0;
    for (uint c = 0; c < curve; ++c) {
        first += uint(max(counts[c], 0));
    }
    if (first >= pointCount) {
        return false;
    }
    const uint count = min(first + uint(max(counts[curve], 0)), pointCount) - first;
    const uint spans = CurveSpans(count, type, basis, wrap);
    if (spans == 0) {
        return false;
    }
    const bool periodic = wrap == CurveWrap::Periodic;
    const uint step = type == CurveType::Cubic && basis == CurveBasis::Bezier ? 3 : 1;
    CurveSpan span;
    float u = position * float(spans);
    if (byDistance) {
        float total = 0;
        for (uint k = 0; k < spans; ++k) {
            MakeCurveSpan(first, count, 0, curve, first + k * step, type, basis, wrap, span);
            total += CurveSpanLength(points, span, 1);
        }
        float along = periodic && total > 0 ? position - total * floor(position / total)
                                            : clamp(position, 0.0f, total);
        placed = along;
        u = float(spans);
        for (uint k = 0; k < spans; ++k) {
            MakeCurveSpan(first, count, 0, curve, first + k * step, type, basis, wrap, span);
            const float spanLength = CurveSpanLength(points, span, 1);
            if (along <= spanLength) {
                u = float(k) + CurveSpanParameter(points, span, along);
                break;
            }
            along -= spanLength;
        }
    }
    u = periodic ? u - float(spans) * floor(u / float(spans)) : clamp(u, 0.0f, float(spans));
    if (!byDistance) {
        placed = u / float(spans);
    }
    const uint k = min(uint(u), spans - 1);
    const float t = saturate(u - float(k));
    MakeCurveSpan(first, count, 0, curve, first + k * step, type, basis, wrap, span);
    at = span.Position(points, t);
    const float h = 1e-3f;
    tangent = span.Position(points, min(t + h, 1.0f)) - span.Position(points, max(t - h, 0.0f));
    return true;
}

float3 CurvePoint(CurvesRecord record, CurveSpan span, float t) {
    return span.Position(Ptr<Float3>(record.points), Ptr<Float3>(record.tangents), t);
}

void CurveBezier(CurvesRecord record, CurveSpan span, out float3 b[4]) {
    const Ptr<Float3> points = Ptr<Float3>(record.points);
    if (span.kind == kCurveKindHermite) {
        const Ptr<Float3> tangents = Ptr<Float3>(record.tangents);
        b[0] = points[span.controls.x].Vector();
        b[1] = b[0] + tangents[span.controls.x].Vector() / 3;
        b[3] = points[span.controls.y].Vector();
        b[2] = b[3] - tangents[span.controls.y].Vector() / 3;
    } else if (span.kind == kCurveKindNurbs) {
        const float3 p0 = CurvePoint(record, span, 0);
        const float3 p1 = CurvePoint(record, span, 1.0f / 3);
        const float3 p2 = CurvePoint(record, span, 2.0f / 3);
        const float3 p3 = CurvePoint(record, span, 1);
        b[0] = p0;
        b[1] = (-5 * p0 + 18 * p1 - 9 * p2 + 2 * p3) / 6;
        b[2] = (2 * p0 - 9 * p1 + 18 * p2 - 5 * p3) / 6;
        b[3] = p3;
    } else {
        span.Bezier(points, b);
    }
}

uint CurveSpanSteps(CurvesRecord record, CurveSpan span, float tolerance, uint maxSteps) {
    if (!span.cubic) {
        return 1;
    }
    if (tolerance <= 0) {
        return maxSteps;
    }
    float3 b[4];
    CurveBezier(record, span, b);
    const float bend = max(length(b[0] - 2 * b[1] + b[2]), length(b[1] - 2 * b[2] + b[3]));
    const float steps = ceil(sqrt(0.75f * bend / tolerance));
    return uint(clamp(steps, 1.0f, float(maxSteps)));
}

#endif

}

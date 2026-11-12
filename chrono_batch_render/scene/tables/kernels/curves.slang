// SPDX-License-Identifier: BSD-3-Clause

import chrono_batch_render.device.kernels.device;

#include "chrono_batch_render/scene/records/bounds.h"
#include "chrono_batch_render/scene/tables/kernels/extents.h"
#include "chrono_batch_render/scene/tables/kernels/curves.h"

using namespace chrono::batch_render;

uint FindJob(CurvesParams p, uint item) {
    uint low = 0;
    uint high = p.jobCount - 1;
    while (low < high) {
        const uint middle = (low + high + 1) / 2;
        if (p.jobs[middle].first <= item) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

uint FindCountJob(CurvesParams p, uint item) {
    uint low = 0;
    uint high = p.jobCount - 1;
    while (low < high) {
        const uint middle = (low + high + 1) / 2;
        if (p.jobs[middle].countFirst <= item) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

struct CurveLayout {
    CurveType type;
    CurveBasis basis;
    CurveWrap wrap;
    Interpolation widths;
    Interpolation normals;
}

CurveLayout JobLayout(CurvesParams p, CurvesJob job) {
    AuthoredData* scene = p.scene;
    CurveLayout layout;
    layout.type = CurveType::Cubic;
    layout.basis = CurveBasis::Bezier;
    layout.wrap = CurveWrap::Nonperiodic;
    if (job.record.kind == kCurveKindBasis) {
        layout.type = CurveType(scene->schemas.basisCurves.type[job.basisRow]);
        layout.basis = CurveBasis(scene->schemas.basisCurves.basis[job.basisRow]);
        layout.wrap = CurveWrap(scene->schemas.basisCurves.wrap[job.basisRow]);
    }
    layout.widths = Interpolation(scene->schemas.curves.widthsInterpolation[job.curvesRow]);
    layout.normals =
        Interpolation(scene->schemas.pointBased.normalsInterpolation[job.pointBasedRow]);
    return layout;
}

uint CurveOrder(CurvesRecord record, uint curve) {
    return record.kind == kCurveKindNurbs && record.orders != 0
               ? uint(max(Ptr<int>(record.orders)[curve], 0))
               : 0;
}

void BeginExtents(CurvesParams p, uint thread, uint item) {
    BeginExtents(thread, thread == 0 ? FindJob(p, min(item, p.total - 1)) : 0);
}

void Extend(CurvesParams p, uint index, float3 lower, float3 upper) {
    Extend(p.extents, p.jobCount, index, lower, upper);
}

void EndExtents(CurvesParams p, uint thread, uint item) {
    EndExtents(p.extents, p.jobCount, thread, item - thread < p.total);
}

[shader("compute")]
[numthreads(kCurvesGroup, 1, 1)]
void GatherCurveCounts(uniform CurvesParams p) {
    const uint item = LinearThreadIndex();
    if (item >= p.countTotal) {
        return;
    }
    const uint index = FindCountJob(p, item);
    const CurvesJob job = p.jobs[index];
    const CurveLayout layout = JobLayout(p, job);
    const uint curve = item - job.countFirst;
    const uint count = uint(max(Ptr<int>(job.record.counts)[curve], 0));
    const uint order = CurveOrder(job.record, curve);
    p.counts[item] = count;
    p.orders[item] = order;
    p.varying[item] = CurveKindVaryingCount(job.record.kind, count, order, layout.type,
                                            layout.basis, layout.wrap);
    if (job.restep != 0) {
        InterlockedAdd(p.totals[index * 2 + 1],
                       CurveKindSpans(job.record.kind, count, order, layout.type, layout.basis,
                                      layout.wrap));
    }
}

[shader("compute")]
[numthreads(kCurvesGroup, 1, 1)]
void WriteCurveStarts(uniform CurvesParams p) {
    const uint item = LinearThreadIndex();
    if (item >= p.countTotal) {
        return;
    }
    const CurvesJob job = p.jobs[FindCountJob(p, item)];
    const uint curve = item - job.countFirst;
    Ptr<uint>(job.record.starts)[curve] = p.scanned[item] - p.scanned[job.countFirst];
    Ptr<uint>(job.record.varyingStarts)[curve] =
        p.scannedVarying[item] - p.scannedVarying[job.countFirst];
    if (job.record.kind == kCurveKindNurbs) {
        Ptr<uint>(job.record.knotStarts)[curve] = p.scanned[item] - p.scanned[job.countFirst] +
                                                  p.scannedOrders[item] -
                                                  p.scannedOrders[job.countFirst];
    }
}

[shader("compute")]
[numthreads(kCurvesGroup, 1, 1)]
void CountCurveSteps(uniform CurvesParams p) {
    const uint item = LinearThreadIndex();
    if (item >= p.total) {
        return;
    }
    const CurvesJob job = p.jobs[FindJob(p, item)];
    uint steps = 0;
    CurveSpan span;
    const CurveLayout layout = JobLayout(p, job);
    if (job.restep != 0 &&
        FindCurveSpan(job.record, item - job.first, layout.type, layout.basis, layout.wrap, span)) {
        float3 b[4];
        CurveBezier(job.record, span, b);
        const float size = length(b[1] - b[0]) + length(b[2] - b[1]) + length(b[3] - b[2]);
        const float radius = max(CurveSpanRadius(job.record, layout.widths, span, 0),
                                 CurveSpanRadius(job.record, layout.widths, span, 1));
        const float tolerance =
            p.tolerance > 0 ? max(p.tolerance * radius, kCurveSizeTolerance * p.tolerance * size)
                            : 0;
        steps = CurveSpanSteps(job.record, span, tolerance, p.maxSteps);
    }
    p.steps[item] = steps;
}

[shader("compute")]
[numthreads(kCurvesGroup, 1, 1)]
void SumCurveSteps(uniform CurvesParams p) {
    const uint index = LinearThreadIndex();
    if (index >= p.jobCount) {
        return;
    }
    const CurvesJob job = p.jobs[index];
    const uint last = job.first + job.record.pointCount - 1;
    p.totals[index * 2] = p.stepFirst[last] + p.steps[last] - p.stepFirst[job.first];
}

[shader("compute")]
[numthreads(kCurvesGroup, 1, 1)]
void FitCurveSteps(uniform CurvesParams p) {
    const uint item = LinearThreadIndex();
    if (item >= p.total) {
        return;
    }
    const uint index = FindJob(p, item);
    const CurvesJob job = p.jobs[index];
    const uint steps = p.steps[item];
    const uint wanted = p.totals[index * 2];
    const uint spans = p.totals[index * 2 + 1];
    const uint capacity = job.record.segmentCount;
    if (job.restep == 0 || steps == 0 || wanted <= capacity) {
        return;
    }
    const float share = capacity > spans ? float(capacity - spans) / float(wanted - spans) : 0;
    p.steps[item] = 1 + uint(float(steps - 1) * share);
}

[shader("compute")]
[numthreads(kCurvesGroup, 1, 1)]
void StoreCurveSteps(uniform CurvesParams p) {
    const uint item = LinearThreadIndex();
    if (item >= p.total) {
        return;
    }
    const CurvesJob job = p.jobs[FindJob(p, item)];
    if (job.restep == 0) {
        return;
    }
    const uint span = item - job.first;
    Ptr<uint>(job.record.spanFirst)[span] = p.stepFirst[item] - p.stepFirst[job.first];
    Ptr<uint>(job.record.spanSteps)[span] = p.steps[item];
}

[shader("compute")]
[numthreads(kCurvesGroup, 1, 1)]
void WriteCurveSegments(uniform CurvesParams p, uint thread: SV_GroupIndex) {
    const uint item = LinearThreadIndex();
    BeginExtents(p, thread, item);
    if (item < p.total) {
        const uint index = FindJob(p, item);
        const CurvesJob job = p.jobs[index];
        const CurvesRecord record = job.record;
        const uint point = item - job.first;
        const uint steps = Ptr<uint>(record.spanSteps)[point];
        const uint first = Ptr<uint>(record.spanFirst)[point];
        const CurveLayout layout = JobLayout(p, job);
        CurveSpan span;
        if (steps > 0 && FindCurveSpan(record, point, layout.type, layout.basis, layout.wrap, span)) {
            const bool exact = p.exact != 0 && span.cubic && record.normalCount == 0;
            float bend = 0;
            if (exact) {
                float3 b[4];
                CurveBezier(record, span, b);
                const float most =
                    max(length(b[0] - 2 * b[1] + b[2]), length(b[1] - 2 * b[2] + b[3]));
                bend = 0.8f * most / float(steps * steps);
            }
            float3 a = CurvePoint(record, span, 0);
            float ra = CurveSpanRadius(record, layout.widths, span, 0);
            float3 lower = float3(kInfinity);
            float3 upper = float3(-kInfinity);
            for (uint step = 0; step < steps && first + step < record.segmentCount; ++step) {
                const float t = float(step + 1) / float(steps);
                const float3 b = CurvePoint(record, span, t);
                const float rb = CurveSpanRadius(record, layout.widths, span, t);
                float pad = 0;
                if (exact) {
                    const float middle =
                        CurveSpanRadius(record, layout.widths, span, t - 0.5f / float(steps));
                    pad = bend + 2 * abs(middle - 0.5f * (ra + rb)) + 1e-6f * (ra + rb);
                }
                CurveSegment segment;
                segment.start = Float4(float4(a, ra + pad));
                segment.end = Float4(float4(b, rb + pad));
                segment.curve = span.curve;
                segment.span = point;
                segment.bits = CurveBits(steps, step, layout.type, layout.basis, layout.wrap) |
                               (uint(layout.widths) << 23) | (exact ? kCurveExact : 0);
                segment.normal = 0;
                if (record.normalCount > 0) {
                    const float3 along = b - a;
                    float3 normal = CurveSpanVector(Ptr<Float3>(record.normals), record.normalCount,
                                                    layout.normals, span, t - 0.5f / float(steps));
                    if (dot(along, along) > 0) {
                        normal -= dot(normal, along) / dot(along, along) * along;
                    }
                    if (dot(normal, normal) > 0) {
                        segment.bits |= kCurveRibbon;
                        segment.normal = PackNormal(normalize(normal));
                    }
                }
                Ptr<CurveSegment>(record.segments)[first + step] = segment;
                BvhBox box;
                const float3 low = min(a - ra, b - rb) - pad;
                const float3 high = max(a + ra, b + rb) + pad;
                for (uint axis = 0; axis < 3; ++axis) {
                    box.lower[axis] = low[axis];
                    box.upper[axis] = high[axis];
                }
                job.boxes[first + step] = box;
                lower = min(lower, low);
                upper = max(upper, high);
                a = b;
                ra = rb;
            }
            Extend(p, index, lower, upper);
        }
    }
    EndExtents(p, thread, item);
}

[shader("compute")]
[numthreads(kCurvesGroup, 1, 1)]
void FinishCurveBounds(uniform CurvesParams p) {
    const uint index = LinearThreadIndex();
    if (index >= p.jobCount) {
        return;
    }
    FinishExtents(p.extents, p.jobCount, index, p.jobs[index].bounds);
}

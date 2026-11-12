// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.scene.records.render_tables;
__exported import chrono_batch_render.render.kernels.shapes.box;
__exported import chrono_batch_render.render.kernels.shapes.capsule;
__exported import chrono_batch_render.render.kernels.shapes.cone;
__exported import chrono_batch_render.render.kernels.shapes.cylinder;
__exported import chrono_batch_render.render.kernels.shapes.plane;
__exported import chrono_batch_render.render.kernels.shapes.round_cone;
__exported import chrono_batch_render.render.kernels.shapes.sphere;
__exported import chrono_batch_render.render.kernels.trace.ray;
import chrono_batch_render.device.kernels.device;
import chrono_batch_render.render.kernels.random;
import chrono_batch_render.render.kernels.shapes.options;
import chrono_batch_render.render.kernels.surface.primvars;

using namespace chrono::batch_render;

ShapeHit CenteredShapeHit(ShapeRecord record, Ray ray) {
    switch (ShapeKind(record.kind)) {
    case ShapeKind::Sphere:
        return Sphere(1).Intersect(ray);
    case ShapeKind::Plane:
        return Plane(2, 2).Intersect(ray);
    case ShapeKind::Cylinder:
        return Cylinder(1, 2).Intersect(ray);
    case ShapeKind::Cone:
        return Cone(1, 2).Intersect(ray);
    case ShapeKind::Disk: {
        ShapeHit hit = CrossZ(ray);
        const float2 at = detach(ray.origin.xy) + detach(hit.t) * detach(ray.direction.xy);
        hit.inside = hit.inside && dot(at, at) <= 1;
        return hit;
    }
    case ShapeKind::Tube:
        return Cylinder(1, 2).Intersect(ray, false);
    case ShapeKind::Capsule: {
        const float stretch = record.param;
        Ray local = ray;
        local.origin.z *= stretch;
        local.direction.z *= stretch;
        ShapeHit hit = Capsule(1, 2 * (stretch - 1)).Intersect(local);
        hit.normal.z *= stretch;
        return hit;
    }
    default:
        return Box(float3(1)).Intersect(ray);
    }
}

Ray Centered(Ray ray, out float closest) {
    closest = -dot(ray.origin, ray.direction) / dot(ray.direction, ray.direction);
    Ray centered = ray;
    centered.origin = ray.origin + closest * ray.direction;
    centered.tMin = ray.tMin - closest;
    return centered;
}

ShapeHit PointHit(PointsRecord cloud, uint point, Ray ray) {
    ray.origin -= Ptr<Float3>(cloud.points)[point].Vector();
    float closest;
    const Ray centered = Centered(ray, closest);
    const float radius = PointRadius(cloud, point);
    float3 normal = float3(0);
    if (cloud.normalCount > 0) {
        normal = Ptr<Float3>(cloud.normals)[cloud.normalCount == cloud.pointCount ? point : 0].Vector();
    }
    ShapeHit hit;
    if (dot(normal, normal) > 0) {
        hit.normal = normalize(normal);
        const float facing = dot(centered.direction, hit.normal);
        hit.t = -dot(centered.origin, hit.normal) / facing;
        const float3 at = centered.origin + hit.t * centered.direction;
        hit.inside = facing != 0 && hit.t >= centered.tMin && dot(at, at) <= radius * radius;
    } else {
        hit = Sphere(radius).Intersect(centered);
    }
    hit.t += closest;
    return hit;
}

CurveSegment LoadCurveSegment(Ptr<CurvesRecord> curves, uint segment) {
    return Ptr<CurveSegment>(curves.segments)[segment];
}

struct TubeProbe {
    float entry;
    float miss;
}

TubeProbe ProbeTube(CurvesRecord curves, CurveSpan span, Interpolation widths, float3 middle,
                    float3 origin, float3 direction, float t) {
    const float3 at = CurvePoint(curves, span, t) - middle;
    const float radius = CurveSpanRadius(curves, widths, span, t);
    const float along = dot(at - origin, direction);
    const float3 across = at - origin - along * direction;
    const float gap = dot(across, across);
    TubeProbe probe;
    probe.miss = sqrt(gap) - radius;
    probe.entry = probe.miss <= 0 ? along - sqrt(max(radius * radius - gap, 0.0f)) : kInfinity;
    return probe;
}

bool Better(TubeProbe a, TubeProbe b) {
    if ((a.miss <= 0) != (b.miss <= 0)) {
        return a.miss <= 0;
    }
    return a.miss <= 0 ? a.entry < b.entry : a.miss < b.miss;
}

RoundConeHit RefineCurveHit(Ptr<CurvesRecord> pointer, CurveSegment segment, Ray ray,
                            RoundConeHit candidate) {
    const CurvesRecord curves = *pointer;
    CurveSpan span;
    if (!FindCurveSpanIn(curves, segment.curve, segment.span, BitsType(segment.bits),
                         BitsBasis(segment.bits), BitsWrap(segment.bits), span)) {
        return candidate;
    }
    const Interpolation widths = BitsWidths(segment.bits);
    const float steps = float(max(BitsSteps(segment.bits), 1u));
    const float step = float(BitsStep(segment.bits));
    const float scale = length(ray.direction);
    const float3 direction = ray.direction / scale;
    const float3 middle = 0.5f * (segment.start.Vector().xyz + segment.end.Vector().xyz);
    float3 origin = ray.origin - middle;
    const float closest = -dot(origin, direction);
    origin += closest * direction;
    const float tMin = ray.tMin * scale - closest;

    const float low = max((step - 1) / steps, 0.0f);
    const float high = min((step + 2) / steps, 1.0f);
    const uint kScan = 12;
    float best = clamp((step + candidate.along) / steps, low, high);
    TubeProbe found = ProbeTube(curves, span, widths, middle, origin, direction, best);
    for (uint i = 0; i <= kScan; ++i) {
        const float t = lerp(low, high, float(i) / float(kScan));
        const TubeProbe probe = ProbeTube(curves, span, widths, middle, origin, direction, t);
        if (Better(probe, found)) {
            found = probe;
            best = t;
        }
    }
    const float golden = 0.381966f;
    float a = max(best - (high - low) / float(kScan), low);
    float b = min(best + (high - low) / float(kScan), high);
    for (uint i = 0; i < 24; ++i) {
        const float c = a + golden * (b - a);
        const float d = b - golden * (b - a);
        const TubeProbe left = ProbeTube(curves, span, widths, middle, origin, direction, c);
        const TubeProbe right = ProbeTube(curves, span, widths, middle, origin, direction, d);
        if (Better(left, right)) {
            b = d;
            if (Better(left, found)) {
                found = left;
                best = c;
            }
        } else {
            a = c;
            if (Better(right, found)) {
                found = right;
                best = d;
            }
        }
    }
    if (found.miss > 0) {
        RoundConeHit miss = candidate;
        miss.hit.inside = false;
        miss.hit.t = kInfinity;
        return miss;
    }
    if (found.entry < tMin) {
        return candidate;
    }
    const float3 point = origin + found.entry * direction;
    const float3 center = CurvePoint(curves, span, best) - middle;
    RoundConeHit result;
    result.hit.t = (found.entry + closest) / scale;
    result.hit.normal = normalize(point - center);
    result.hit.inside = true;
    result.along = best * steps - step;
    return result;
}

RoundConeHit CurveHit(Ptr<CurvesRecord> curves, CurveSegment segment, Ray ray) {
    const float4 start = segment.start.Vector();
    const float4 end = segment.end.Vector();
    if (BitsRibbon(segment.bits)) {
        return IntersectRibbon(start.xyz, start.w, end.xyz, end.w, UnpackNormal(segment.normal),
                               ray.origin, ray.direction, ray.tMin);
    }
    const RoundConeHit candidate = IntersectRoundCone(start.xyz, start.w, end.xyz, end.w,
                                                      ray.origin, ray.direction, ray.tMin);
    if (!kExactCurves || !BitsExact(segment.bits) || !candidate.hit.inside) {
        return candidate;
    }
    return RefineCurveHit(curves, segment, ray, candidate);
}

ShapeHit LocalShapeHit(ShapeRecord record, RenderTables* tables, uint primitive, Ray ray) {
    if (ShapeKind(record.kind) == ShapeKind::Points) {
        return PointHit(tables->points[record.row], primitive, ray);
    }
    if (kCurves && ShapeKind(record.kind) == ShapeKind::Curves) {
        const CurveSegment segment = LoadCurveSegment(tables->curves + record.row, primitive);
        if (segment.curve == kNoCurve) {
            ShapeHit miss;
            miss.t = kInfinity;
            miss.normal = float3(0, 0, 1);
            miss.inside = false;
            return miss;
        }
        return CurveHit(tables->curves + record.row, segment, ray).hit;
    }
    float closest;
    ShapeHit hit = CenteredShapeHit(record, Centered(ray, closest));
    hit.t += closest;
    return hit;
}

#if !CHRONO_BATCH_RENDER_VULKAN
[noinline]
#endif
ShapeHit IntersectShape(ShapeRecord record, RenderTables* tables, uint primitive, Ray ray) {
    return LocalShapeHit(record, tables, primitive, ray);
}

#if !CHRONO_BATCH_RENDER_VULKAN
[noinline]
#endif
float ShapeDistance(ShapeRecord record, RenderTables* tables, uint primitive, Ray ray) {
    const ShapeHit hit = LocalShapeHit(record, tables, primitive, ray);
    return hit.inside ? hit.t : kInfinity;
}

float ReadOpacity(Ptr<SurfaceRecord> surface, uint at, float fallback) {
    const uint indexCount = surface.opacityIndexCount;
    if (indexCount > 0) {
        at = at < indexCount ? Ptr<uint>(surface.opacityIndices)[at] : ~0u;
    }
    if (at >= surface.opacityCount) {
        return fallback;
    }
    const uint word = Ptr<uint>(surface.opacities)[at * surface.opacityStride];
    return (surface.opacityFlags & kOpacityInteger) != 0 ? float(int(word)) : asfloat(word);
}

bool PointBlocks(Ptr<SurfaceRecord> surface, uint point, Ray ray) {
    float opacity = surface.displayOpacity;
    if (surface.opacities != 0) {
        opacity = ReadOpacity(surface, (surface.opacityFlags & kOpacityPerPoint) != 0 ? point : 0,
                            opacity);
    }
    return Keeps(point, ray, opacity);
}

bool CurveBlocks(Ptr<SurfaceRecord> surface, Ptr<CurvesRecord> curves, CurveSegment segment,
                 uint primitive, float along, Ray ray) {
    float opacity = surface.displayOpacity;
    if (surface.opacities != 0) {
        const uint flags = surface.opacityFlags;
        if ((flags & kOpacityPerPoint) == 0) {
            opacity = ReadOpacity(surface, 0, opacity);
        } else {
            CurveSpan span;
            FindCurveSpanIn(*curves, segment.curve, segment.span, BitsType(segment.bits),
                            BitsBasis(segment.bits), BitsWrap(segment.bits), span);
            const float t = (float(BitsStep(segment.bits)) + along) /
                            float(max(BitsSteps(segment.bits), 1u));
            uint4 indices;
            float4 weights;
            ElementBlend(SurfaceElement.Curve(primitive, span, t),
                         Interpolation(flags >> kOpacityInterpolationShift), indices, weights);
            const float fallback = opacity;
            opacity = 0;
            for (uint i = 0; i < 4; ++i) {
                if (weights[i] != 0) {
                    opacity += weights[i] * ReadOpacity(surface, indices[i], fallback);
                }
            }
        }
    }
    return Keeps(primitive, ray, opacity);
}

bool Keeps(uint element, Ray ray, float opacity) {
    if (opacity >= 1) {
        return true;
    }
    uint seed = Hash(element);
    for (uint axis = 0; axis < 3; ++axis) {
        seed = Hash(seed ^ asuint(ray.origin[axis]));
        seed = Hash(seed ^ asuint(ray.direction[axis]));
    }
    return float(seed >> 8) * (1.0f / 16777216.0f) < opacity;
}

struct ShapeIntersector : IIntersector {
    RenderTables* tables;
    uint tlas;

    bool Intersect(Ray ray, uint instance, uint geometry, uint primitive, inout float t,
                   out float2 attributes) {
        attributes = float2(0);
        InstanceLayout* layout = tables->instances;
        const uint slot = layout->firstInstances[tlas] + instance;
        const ShapeRecord shape = layout->shapes[slot];
        if (ShapeKind(shape.kind) == ShapeKind::Points) {
            return IntersectPoints(tables->points[shape.row], tables->surfaces + slot, primitive, ray,
                                   t, attributes);
        }
        if (kCurves && ShapeKind(shape.kind) == ShapeKind::Curves) {
            return IntersectCurves(tables->curves + shape.row, tables->surfaces + slot, primitive,
                                   ray, t, attributes);
        }
        const float distance = ShapeDistance(shape, tables, primitive, ray);
        if (distance >= t) {
            return false;
        }
        t = distance;
        return true;
    }

    bool IntersectPoints(PointsRecord cloud, Ptr<SurfaceRecord> surface, uint box, Ray ray,
                         inout float t, inout float2 attributes) {
        const uint perBox = max(cloud.perBox, 1u);
        const uint start = box * perBox;
        const uint end = min(start + perBox, cloud.pointCount);
        bool found = false;
        const float reach = perBox > 1 ? 1 / dot(ray.direction, ray.direction) : 0;
        for (uint at = start; at < end; ++at) {
            if (perBox > 1) {
                const float4 sphere = Ptr<float4>(cloud.packed)[at];
                const float3 offset = ray.origin - sphere.xyz;
                const float3 miss = offset - dot(offset, ray.direction) * reach * ray.direction;
                const float slack = sphere.w * 1.001f + 1e-5f * (abs(offset.x) + abs(offset.y) + abs(offset.z));
                if (dot(miss, miss) > slack * slack) {
                    continue;
                }
            }
            const uint point = perBox == 1 ? at : Ptr<uint>(cloud.order)[at];
            const ShapeHit hit = PointHit(cloud, point, ray);
            if (!hit.inside || hit.t >= t || !PointBlocks(surface, point, ray)) {
                continue;
            }
            t = hit.t;
            attributes = float2(asfloat(point), 0);
            found = true;
        }
        return found;
    }

    bool IntersectCurves(Ptr<CurvesRecord> curves, Ptr<SurfaceRecord> surface, uint primitive, Ray ray,
                         inout float t, inout float2 attributes) {
        const CurveSegment segment = LoadCurveSegment(curves, primitive);
        if (segment.curve == kNoCurve) {
            return false;
        }
        const RoundConeHit found = CurveHit(curves, segment, ray);
        if (!found.hit.inside || found.hit.t >= t ||
            !CurveBlocks(surface, curves, segment, primitive, found.along, ray)) {
            return false;
        }
        t = found.hit.t;
        attributes = float2(found.along, 0);
        return true;
    }

    [Differentiable]
    [NoDiffThis]
    Hit Reintersect(Ray ray, no_diff Hit found) {
        return found;
    }
}

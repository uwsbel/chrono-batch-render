// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.scene.records.render_tables;
__exported import chrono_batch_render.render.kernels.trace.ray;

using namespace chrono::batch_render;

struct SurfaceElement {
    bool mesh;
    bool indexed;
    uint triangle;
    uint face;
    uint3 corners;
    uint3 points;
    float3 weights;
    bool curve;
    uint4 controls;
    float4 basis;
    uint2 ends;
    float across;

    static SurfaceElement Shape(uint primitive) {
        SurfaceElement element;
        element.mesh = false;
        element.indexed = false;
        element.triangle = primitive;
        element.face = 0;
        element.corners = uint3(0);
        element.points = uint3(0);
        element.weights = float3(1, 0, 0);
        element.curve = false;
        element.controls = uint4(0);
        element.basis = float4(1, 0, 0, 0);
        element.ends = uint2(0);
        element.across = 0;
        return element;
    }

    static SurfaceElement Point(uint index) {
        SurfaceElement element = Shape(index);
        element.indexed = true;
        element.face = index;
        element.corners = uint3(index);
        element.points = uint3(index);
        return element;
    }

    static SurfaceElement Curve(uint segment, CurveSpan span, float t) {
        SurfaceElement element = Shape(segment);
        element.indexed = true;
        element.curve = true;
        element.face = span.curve;
        element.controls = span.controls;
        element.basis = span.Weights(t);
        element.ends = span.ends;
        element.across = t;
        element.corners = span.controls.xyz;
        element.points = span.controls.xyz;
        element.weights = element.basis.xyz;
        return element;
    }
}

SurfaceElement FindSurfaceElement(SurfaceRecord surface, Hit hit) {
    SurfaceElement element = SurfaceElement.Shape(hit.primitive);
    if (surface.corners == 0) {
        return element;
    }
    element.mesh = true;
    element.indexed = true;
    const Ptr<uint> corners = Ptr<uint>(surface.corners);
    const Ptr<int> indices = Ptr<int>(surface.indices);
    element.corners = uint3(corners[hit.primitive * 3 + 0], corners[hit.primitive * 3 + 1],
                            corners[hit.primitive * 3 + 2]);
    element.points = uint3(indices[element.corners.x], indices[element.corners.y],
                           indices[element.corners.z]);
    element.face = Ptr<uint>(surface.faces)[hit.primitive];
    element.weights = float3(1 - hit.barycentrics.x - hit.barycentrics.y, hit.barycentrics.x,
                             hit.barycentrics.y);
    return element;
}

void ElementBlend(SurfaceElement element, Interpolation interpolation, out uint4 indices,
                  out float4 weights) {
    if (!element.curve) {
        indices = uint4(InterpolationIndices(element, interpolation), 0);
        weights = float4(interpolation == Interpolation::Constant ? float3(1, 0, 0) : element.weights,
                         0);
        return;
    }
    switch (interpolation) {
    case Interpolation::Constant:
        indices = uint4(0);
        weights = float4(1, 0, 0, 0);
        return;
    case Interpolation::Uniform:
        indices = uint4(element.face);
        weights = float4(1, 0, 0, 0);
        return;
    case Interpolation::Vertex:
        indices = element.controls;
        weights = element.basis;
        return;
    default:
        indices = element.ends.xyyy;
        weights = float4(1 - element.across, element.across, 0, 0);
        return;
    }
}

uint3 InterpolationIndices(SurfaceElement element, Interpolation interpolation) {
    switch (interpolation) {
    case Interpolation::Constant:
        return uint3(0);
    case Interpolation::Uniform:
        return uint3(element.face);
    case Interpolation::FaceVarying:
        return element.corners;
    default:
        return element.points;
    }
}

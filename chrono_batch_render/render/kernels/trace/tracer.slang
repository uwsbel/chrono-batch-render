// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.render.kernels.trace.software;

#if CHRONO_BATCH_RENDER_HARDWARE_TRAVERSAL
__exported import chrono_batch_render.render.kernels.trace.hardware;
typealias Tracer = HardwareTracer;
#else
typealias Tracer = SoftwareTracer;
#endif

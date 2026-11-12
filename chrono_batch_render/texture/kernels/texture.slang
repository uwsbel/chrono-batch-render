// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.texture.kernels.software;

#if CHRONO_BATCH_RENDER_OBJECT_TEXTURES || CHRONO_BATCH_RENDER_HARDWARE_TEXTURES
__exported import chrono_batch_render.texture.kernels.hardware;
#if CHRONO_BATCH_RENDER_OBJECT_TEXTURES
typealias SampledImage = chrono::batch_render::TextureObjects;
#else
typealias SampledImage = BindlessImage;
#endif
typealias SampledTexture = HardwareTexture<SampledImage>;
typealias SampledUdimSet = HardwareUdimSet<SampledImage>;
#else
typealias SampledTexture = SoftwareTexture;
typealias SampledUdimSet = UdimSet;
#endif

SampledTexture TextureFromHandle(uint64_t handle) {
#if CHRONO_BATCH_RENDER_OBJECT_TEXTURES || CHRONO_BATCH_RENDER_HARDWARE_TEXTURES
    return SampledTexture(SampledImage.FromHandle(handle));
#else
    return SoftwareTexture(Ptr<chrono::batch_render::TextureDesc>(handle));
#endif
}

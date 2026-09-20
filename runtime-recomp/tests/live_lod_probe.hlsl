// Test-only: use the renderer's actual sampling code with coloured mip levels.
#include "TextureSampler.hlsli"

struct ProbeParams { float bias; uint mipCount; uint textureIndex; uint padding; };
[[vk::push_constant]] ConstantBuffer<ProbeParams> probe : register(b0, space5);
RWStructuredBuffer<float4> results : register(u0, space4);

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    OtherMode mode = (OtherMode)0;
    mode.H = G_TP_PERSP;
    RDPTile tile = (RDPTile)0;
    tile.shifts = tile.shiftt = 1.0;
    tile.masks = tile.maskt = 6;
    tile.lrs = tile.lrt = 252.0;
    tile.nativeSampler = id.x; // 0 = manual; 1..9 = all native wrap modes.
    GPUTile gpu = (GPUTile)0;
    gpu.ulScale = gpu.tcScale = float2(1.0, 1.0);
    gpu.texelMask = uint2(0xffffffff, 0xffffffff);
    gpu.textureIndex = probe.textureIndex;
    gpu.textureDimensions = float3(64.0, 64.0, probe.mipCount);
    gpu.flags = 0x2 | ((probe.mipCount > 1) ? 0x10 : 0);
    results[id.x] = sampleTexture(mode, 0, float2(32.0, 32.0),
        float2(4.0, 0.0), float2(0.0, 4.0), tile, gpu, false, probe.bias);
}

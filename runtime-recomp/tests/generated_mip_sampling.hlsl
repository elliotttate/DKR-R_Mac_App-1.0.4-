#include "TextureSampler.hlsli"
struct Probe { float bias; uint index; uint scenario; uint padding; };
[[vk::push_constant]] ConstantBuffer<Probe> probe : register(b0,space5);
RWStructuredBuffer<float4> results : register(u0,space4);
[numthreads(1,1,1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    OtherMode mode=(OtherMode)0;mode.H=G_TP_PERSP;mode.L=Z_CMP|Z_UPD;
    RDPTile tile=(RDPTile)0;tile.shifts=tile.shiftt=1;
    tile.masks=tile.maskt=64;tile.lrs=tile.lrt=252;tile.nativeSampler=id.x;
    GPUTile gpu=(GPUTile)0;gpu.ulScale=gpu.tcScale=float2(1,1);
    gpu.texelMask=uint2(0xffffffff,0xffffffff);gpu.textureIndex=probe.index;
    gpu.textureDimensions=float3(64,64,probe.index==2 ? 1:7);
    gpu.flags=(probe.index==1 ? 2:0)|(probe.index==2 ? 0:0x50);
    uint flags=probe.scenario==1 ? 1:0;
    if(probe.scenario==2)gpu.flags|=8;
    if(probe.scenario==3)gpu.flags|=4;
    if(probe.scenario==4)mode.L|=FORCE_BL;
    if(probe.scenario==5)mode.L|=CVG_X_ALPHA;
    if(probe.scenario==6){tile.masks=16;tile.lrs=60;}
    if(probe.scenario==7)mode.L|=G_AC_THRESHOLD;
    if(probe.scenario==8)mode.L|=ZMODE_DEC;
    // 9 models an orthographic/UI scope or Accurate-mode sampling gate.
    results[id.x]=sampleTexture(mode,flags,float2(18.5,18.5),float2(4,0),float2(0,4),tile,gpu,false,probe.bias,probe.scenario!=9);
}

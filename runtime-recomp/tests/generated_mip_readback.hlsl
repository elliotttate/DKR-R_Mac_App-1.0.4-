struct Params { uint width; uint height; uint mip; uint offset; };
[[vk::push_constant]] ConstantBuffer<Params> p : register(b0);
Texture2D<float4> sourceTexture : register(t1);
RWStructuredBuffer<uint> outputPixels : register(u2);
[numthreads(8,8,1)]
void CSMain(uint2 pos : SV_DispatchThreadID) {
    if (any(pos >= uint2(p.width,p.height))) return;
    uint4 c=uint4(round(saturate(sourceTexture.Load(int3(pos,p.mip)))*255.0));
    uint pitch=(p.width*4+255)&~255;
    outputPixels[(p.offset+pos.y*pitch)/4+pos.x]=c.r | (c.g<<8) | (c.b<<16) | (c.a<<24);
}

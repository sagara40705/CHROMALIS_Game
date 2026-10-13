cbuffer DrawConstants : register(b0) {
    row_major float4x4 worldViewProjection;
    row_major float4x4 world;
    float4 baseColor;
    float4 options; // cutoff, alphaMask, unlit, unused
};
Texture2D<float4> baseTexture : register(t0);
SamplerState baseSampler : register(s0);
#ifdef SKINNED
cbuffer SkinConstants : register(b1) {
    row_major float4x4 bones[256];
    row_major float4x4 boneNormals[256];
};
#endif
struct VSInput { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0;
#ifdef SKINNED
    uint4 joints : BLENDINDICES; float4 weights : BLENDWEIGHT;
#endif
};
struct VSOutput { float4 position : SV_Position; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
VSOutput vs_main(VSInput v) {
#ifdef SKINNED
    float3 p=0, n=0;
    [unroll] for(uint i=0;i<4;++i) {
        p+=mul(float4(v.position,1),bones[v.joints[i]]).xyz*v.weights[i];
        n+=mul(float4(v.normal,0),boneNormals[v.joints[i]]).xyz*v.weights[i];
    }
    v.position=p; v.normal=n;
#endif
    VSOutput o; o.position = mul(float4(v.position,1), worldViewProjection);
    o.normal = mul(float4(v.normal,0), world).xyz; o.uv = v.uv; return o;
}
float4 ps_main(VSOutput v, bool front : SV_IsFrontFace) : SV_Target {
    float4 color = baseTexture.Sample(baseSampler,v.uv)*baseColor;
    if (options.y > 0.5) clip(color.a-options.x);
    float3 n = normalize(v.normal)*(front ? 1 : -1);
    float light = options.z > 0.5 ? 1 : 0.3+0.7*saturate(dot(n,normalize(float3(-0.4,0.8,-0.6))));
    // sRGB RTVが線形合成後の出力変換を担当する。
    return float4(saturate(color.rgb*light),1);
}

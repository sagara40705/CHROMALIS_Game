cbuffer DrawConstants : register(b0) {
    row_major float4x4 worldViewProjection;
    row_major float4x4 world;
    float4 baseColor;
    float4 options; // cutoff, alphaMask, unlit, unused
};
Texture2D<float4> baseTexture : register(t0);
SamplerState baseSampler : register(s0);
struct VSInput { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct VSOutput { float4 position : SV_Position; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
VSOutput vs_main(VSInput v) {
    VSOutput o; o.position = mul(float4(v.position,1), worldViewProjection);
    o.normal = mul(float4(v.normal,0), world).xyz; o.uv = v.uv; return o;
}
float3 LinearToSrgb(float3 value) {
    return float3(value.x <= 0.0031308 ? 12.92*value.x : 1.055*pow(value.x,1.0/2.4)-0.055,
                  value.y <= 0.0031308 ? 12.92*value.y : 1.055*pow(value.y,1.0/2.4)-0.055,
                  value.z <= 0.0031308 ? 12.92*value.z : 1.055*pow(value.z,1.0/2.4)-0.055);
}
float4 ps_main(VSOutput v, bool front : SV_IsFrontFace) : SV_Target {
    float4 color = baseTexture.Sample(baseSampler,v.uv)*baseColor;
    if (options.y > 0.5) clip(color.a-options.x);
    float3 n = normalize(v.normal)*(front ? 1 : -1);
    float light = options.z > 0.5 ? 1 : 0.3+0.7*saturate(dot(n,normalize(float3(-0.4,0.8,-0.6))));
    return float4(LinearToSrgb(saturate(color.rgb*light)),1);
}

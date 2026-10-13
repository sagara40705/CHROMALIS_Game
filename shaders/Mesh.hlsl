cbuffer DrawConstants : register(b0) {
    row_major float4x4 worldViewProjection;
    row_major float4x4 world;
    row_major float4x4 normalWorld;
    float4 baseColor;
    float4 options; // 閾値、MASK、シェーディング方式、予約
    float4 emission;
    float4 toon; // 明暗境界、境界の幅
    float4 shadowColor;
    float4 lightDirection;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 lightViewProjection;
    float4 shadowOptions; // 濃さ、深度Bias、Texel幅、PCF半径
    float4 castShadowTint;
    float4 shadowExtra; // 法線Bias（World単位）
};
Texture2D<float4> baseTexture : register(t0);
SamplerState baseSampler : register(s0);
Texture2D<float> shadowTexture : register(t1);
SamplerComparisonState shadowSampler : register(s1);
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
struct VSOutput { float4 position : SV_Position; float3 normal : NORMAL; float2 uv : TEXCOORD0; float3 worldPosition : TEXCOORD1; };
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
    o.normal = mul(float4(v.normal,0), normalWorld).xyz; o.uv = v.uv;
    o.worldPosition = mul(float4(v.position, 1), world).xyz; return o;
}
void shadow_ps(VSOutput v) {
    // 本描画と同じUV・アルファ係数・閾値で、抜き形状を深度へ反映する。
    if (options.y > 0.5) clip(baseTexture.Sample(baseSampler, v.uv).a * baseColor.a - options.x);
}
float3 ShadowLight(VSOutput v, float3 normal, float ndotl) {
    if (shadowOptions.x <= 0) return 1;
    float3 offset = normal * shadowExtra.x * (1 - ndotl);
    float4 projected = mul(float4(v.worldPosition + offset, 1), lightViewProjection);
    float3 projectedPoint = projected.xyz / projected.w;
    float2 uv = projectedPoint.xy * float2(0.5, -0.5) + 0.5;
    // 対象範囲外は明部とし、端の深度を引き延ばさない。
    if (any(uv < 0) || any(uv > 1) || projectedPoint.z < 0 || projectedPoint.z > 1) return 1;
    float depth = projectedPoint.z - shadowOptions.y;
    float visibility = 0;
    if (shadowOptions.w == 0) visibility = shadowTexture.SampleCmpLevelZero(shadowSampler, uv, depth);
    else {
        [unroll] for (int y = -1; y <= 1; ++y)
            [unroll] for (int x = -1; x <= 1; ++x)
                visibility += shadowTexture.SampleCmpLevelZero(shadowSampler, uv + float2(x, y) * shadowOptions.z * shadowOptions.w, depth) / 9;
    }
    return lerp(1.0, castShadowTint.rgb, (1 - visibility) * shadowOptions.x);
}
float4 ps_main(VSOutput v, bool front : SV_IsFrontFace) : SV_Target {
    float4 color = baseTexture.Sample(baseSampler,v.uv)*baseColor;
    if (options.y > 0.5) clip(color.a-options.x);
    float3 n = normalize(v.normal)*(front ? 1 : -1);
    float ndotl = saturate(dot(n, normalize(lightDirection.xyz)));
    float3 shadow = ShadowLight(v, n, ndotl);
    float3 light = ambientColor.rgb + lightColor.rgb * ndotl * shadow;
    if (options.z > 1.5) {
        float band = smoothstep(toon.x - toon.y, toon.x + toon.y, ndotl);
        light = ambientColor.rgb + lightColor.rgb * lerp(shadowColor.rgb, 1.0, band) * shadow;
    } else if (options.z > 0.5) light = 1;
    // sRGB RTVが線形合成後の出力変換を担当する。
    return float4(saturate(color.rgb * light + emission.rgb), 1);
}

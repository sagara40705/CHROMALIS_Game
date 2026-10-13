cbuffer DrawConstants : register(b0) {
    row_major float4x4 worldViewProjection;
    row_major float4x4 world;
    row_major float4x4 normalWorld;
    float4 baseColor;
    float4 options; // 閾値、MASK、シェーディング方式、BLEND
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
    float4 pbr; // Metallic、Roughness、Normal強度、AO強度
    float4 camera;
    float4 maps; // Normal Map、接線、MR Map、AO Mapの有無
};
Texture2D<float4> baseTexture : register(t0);
SamplerState baseSampler : register(s0);
Texture2D<float4> mrTexture : register(t1);
Texture2D<float4> normalTexture : register(t2);
Texture2D<float4> aoTexture : register(t3);
Texture2D<float4> emissiveTexture : register(t4);
SamplerState mrSampler : register(s1);
SamplerState normalSampler : register(s2);
SamplerState aoSampler : register(s3);
SamplerState emissiveSampler : register(s4);
Texture2D<float> shadowTexture : register(t5);
SamplerComparisonState shadowSampler : register(s5);
#ifdef SKINNED
cbuffer SkinConstants : register(b1) {
    row_major float4x4 bones[256];
    row_major float4x4 boneNormals[256];
};
#endif
struct VSInput { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; float4 tangent : TANGENT;
#ifdef SKINNED
    uint4 joints : BLENDINDICES; float4 weights : BLENDWEIGHT;
#endif
};
struct VSOutput { float4 position : SV_Position; float3 normal : NORMAL; float2 uv : TEXCOORD0; float3 worldPosition : TEXCOORD1; float4 tangent : TEXCOORD2; };
VSOutput vs_main(VSInput v) {
#ifdef SKINNED
    float3 p=0, n=0, t=0;
    [unroll] for(uint i=0;i<4;++i) {
        p+=mul(float4(v.position,1),bones[v.joints[i]]).xyz*v.weights[i];
        n+=mul(float4(v.normal,0),boneNormals[v.joints[i]]).xyz*v.weights[i];
        t+=mul(float4(v.tangent.xyz,0),bones[v.joints[i]]).xyz*v.weights[i];
    }
    v.position=p; v.normal=n; v.tangent.xyz=t;
#endif
    VSOutput o; o.position = mul(float4(v.position,1), worldViewProjection);
    o.normal = mul(float4(v.normal,0), normalWorld).xyz; o.uv = v.uv;
    o.tangent = float4(mul(float4(v.tangent.xyz, 0), world).xyz, v.tangent.w * camera.w);
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
// UVと変形後の位置から接線基底を再構成する。退化UVは幾何法線を保つ。
float3 SurfaceNormal(VSOutput v, bool front) {
    float3 n = normalize(v.normal);
    float3 dp1 = ddx(v.worldPosition), dp2 = ddy(v.worldPosition);
    float2 duv1 = ddx(v.uv), duv2 = ddy(v.uv);
    float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
    bool authored = maps.y > 0.5 && abs(v.tangent.w) > 0.5;
    if (maps.x > 0.5 && (authored || abs(determinant) > 1e-12)) {
        float3 t = authored ? v.tangent.xyz : (dp1 * duv2.y - dp2 * duv1.y) / determinant;
        float3 b = (dp2 * duv1.x - dp1 * duv2.x) / determinant;
        t = normalize(t - n * dot(n, t));
        float handedness = authored ? v.tangent.w : (dot(cross(n, t), b) < 0 ? -1 : 1);
        b = cross(n, t) * handedness;
        float3 sampled = normalTexture.Sample(normalSampler, v.uv).xyz * 2 - 1;
        sampled.xy *= pbr.z;
        n = normalize(t * sampled.x + b * sampled.y + n * sampled.z);
    }
    return n * (front ? 1 : -1);
}
float3 EvaluatePbr(float3 color, float3 n, float3 viewDirection, float3 l, float metallic, float roughness) {
    const float pi = 3.14159265359;
    float3 h = normalize(viewDirection + l + 1e-7);
    float nl = saturate(dot(n, l)), nv = max(saturate(dot(n, viewDirection)), 1e-5);
    float nh = saturate(dot(n, h)), vh = saturate(dot(viewDirection, h));
    float alpha = max(roughness * roughness, 0.0025);
    float a2 = alpha * alpha;
    float d = nh * nh * (a2 - 1) + 1;
    float distribution = a2 / (pi * d * d);
    float visibility = 0.5 / max(nl * sqrt(nv * nv * (1 - a2) + a2) + nv * sqrt(nl * nl * (1 - a2) + a2), 1e-6);
    float3 f0 = lerp(0.04, color, metallic);
    float3 fresnel = f0 + (1 - f0) * pow(1 - vh, 5);
    return ((1 - fresnel) * (1 - metallic) * color / pi + distribution * visibility * fresnel) * nl;
}
float4 ps_main(VSOutput v, bool front : SV_IsFrontFace) : SV_Target {
    float4 color = baseTexture.Sample(baseSampler,v.uv)*baseColor;
    if (options.y > 0.5) clip(color.a-options.x);
    float3 n = SurfaceNormal(v, front);
    float ndotl = saturate(dot(n, normalize(lightDirection.xyz)));
    float3 shadow = ShadowLight(v, n, ndotl);
    float3 light = ambientColor.rgb + lightColor.rgb * ndotl * shadow;
    if (options.z > 1.5 && options.z < 2.5) {
        float band = smoothstep(toon.x - toon.y, toon.x + toon.y, ndotl);
        light = ambientColor.rgb + lightColor.rgb * lerp(shadowColor.rgb, 1.0, band) * shadow;
    } else if (options.z > 0.5) light = 1;
    float ao = lerp(1, aoTexture.Sample(aoSampler, v.uv).r, pbr.w * maps.w);
    float3 result = color.rgb * light;
    if (options.z > 2.5) {
        float4 mr = maps.z > 0.5 ? mrTexture.Sample(mrSampler, v.uv) : 1;
        float metallic = saturate(pbr.x * mr.b), roughness = saturate(pbr.y * mr.g);
        float3 viewDirection = normalize(camera.xyz - v.worldPosition);
        result = EvaluatePbr(color.rgb, n, viewDirection, normalize(lightDirection.xyz), metallic, roughness) * lightColor.rgb * shadow;
        // 環境反射は別途IBLが必要。ここでは拡散環境光だけを加える。
        result += ambientColor.rgb * color.rgb * (1 - metallic) * ao;
    }
    result += emission.rgb * (emission.w > 0.5 ? emissiveTexture.Sample(emissiveSampler, v.uv).rgb : 1);
    // Straight Alphaを線形空間で合成し、sRGB RTVで符号化する。
    return float4(result, options.w > 0.5 ? color.a : 1);
}

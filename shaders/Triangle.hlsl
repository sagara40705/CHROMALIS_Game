// Phase 0A diagnostic only: display-encoded RGB into an SDR UNORM target.
// Matrices are row_major; vectors multiply on the left (mul(v, M)).
cbuffer FrameConstants : register(b0)
{
    float2 offset;
    float aspect;
    float scale;
};
struct VertexOutput { float4 position : SV_Position; float3 color : COLOR0; };
VertexOutput vs_main(uint vertex : SV_VertexID)
{
    const float2 positions[3] = { float2(0, 0.55), float2(0.5, -0.4), float2(-0.5, -0.4) };
    const float3 colors[3] = { float3(1, 0.25, 0.3), float3(0.25, 1, 0.45), float3(0.25, 0.5, 1) };
    VertexOutput output;
    float2 p = positions[vertex] * scale;
    p.x /= aspect;
    output.position = float4(p + offset + float2(0.35, 0), 0.5, 1);
    output.color = colors[vertex];
    return output;
}
float4 ps_main(VertexOutput input) : SV_Target { return float4(input.color, 1); }

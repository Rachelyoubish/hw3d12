cbuffer CBuf : register(b0)
{
    row_major matrix transform;
};

struct VSOut
{
    float3 color : Color;
    float4 pos : SV_Position;
};

VSOut main( float2 pos : POSITION, float3 color : COLOR )
{
    VSOut vso;
    vso.pos = mul(float4(pos.x, pos.y, 0.0f, 1.0f), transform);
    vso.color = color;
    return vso;
}
struct vs_in
{
    [[vk::location(0)]] float2 position: POSITION;
    [[vk::location(1)]] float3 color: COLOR;
};

struct vs_out
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

vs_out vs_main(vs_in input)
{
    /*float2 position[3] = {
        float2(0.0, -0.5),
        float2(0.5, 0.5),
        float2(-0.5, 0.5)};

    float3 color[3] = {
        float3(1.0, 0.0, 0.0),
        float3(0.0, 1.0, 0.0),
        float3(0.0, 0.0, 1.0)};*/

    vs_out output;

    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;

    return output;
}

float4 ps_main(vs_out input) : SV_TARGET
{
    return float4(input.color, 1.0);
}
// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../MainGeometry.h"
#include "../Transform.h"

struct PCOut
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

void WriteWire(inout LineStream<PCOut> outputStream, float3 worldStart, float3 worldEnd, float4 color)
{
    PCOut A, B;
    A.position = mul(WorldToClip, float4(worldStart,1));
    A.color = color;
    B.position = mul(WorldToClip, float4(worldEnd,1));
    B.color = color;
    outputStream.Append(A);
    outputStream.Append(B);
    outputStream.RestartStrip();
}

[maxvertexcount(18)]
    void NormalsAndTangents(triangle VSOutput input[3], inout LineStream<PCOut> outputStream)
{
        // for each vertex, we will create a tiny line for the normal, tangent and bitangent
    const float4 tangentColor = float4(1.f, 0.f, 0.f, 1.f);
    const float4 bitangentColor = float4(0.f, 1.f, 0.f, 1.f);
    const float4 normalColor = float4(0.f, 0.f, 1.f, 1.f);
    const float wireLength = 0.015f;
    for (uint c=0; c<3; ++c) {
        float3 pos = input[c].worldPosition;
        // pos += 0.01f * input[c].normal;
        #if OUTPUT_TANGENT_FRAME==1
            WriteWire(outputStream, pos, pos + wireLength * input[c].tangent, tangentColor);
            WriteWire(outputStream, pos, pos + wireLength * input[c].bitangent, bitangentColor);
        #endif
        #if (OUTPUT_NORMAL==1)
            WriteWire(outputStream, pos, pos + wireLength * input[c].normal, normalColor);
        #endif
    }
}

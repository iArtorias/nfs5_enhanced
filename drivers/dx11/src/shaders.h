#pragma once

// HLSL sources, compiled at start up.
// The scene shader emulates the D3D7 fixed function pipeline for pre transformed
// vertices. Two texture stages, specular add, alpha test and fog.

namespace dx11z::shaders
{
    inline constexpr char SCENE[] = R"hlsl(
cbuffer DrawConstants : register( b0 )
{
    float4 g_Target; // width, height, 1 / width, 1 / height
    int4 g_ColorOp[2]; // op, arg1, arg2, unused
    int4 g_AlphaOp[2];
    float4 g_Factor;
    float4 g_FogColor;
    float4 g_FogParams; // start, end, density, table mode
    int4 g_Flags; // fog enable, specular enable, alpha func, texture mask
    float4 g_AlphaRef; // reference 0 to 1
};

Texture2D g_Texture0 : register( t0 );
Texture2D g_Texture1 : register( t1 );
SamplerState g_Sampler0 : register( s0 );
SamplerState g_Sampler1 : register( s1 );

struct VertexIn
{
    float4 m_Position : POSITION;
    float4 m_Diffuse : COLOR0;
    float4 m_Specular : COLOR1;
    float2 m_Uv0 : TEXCOORD0;
    float2 m_Uv1 : TEXCOORD1;
};

struct PixelIn
{
    float4 m_Position : SV_Position;
    float4 m_Diffuse : COLOR0;
    float4 m_Specular : COLOR1;
    float4 m_Uv : TEXCOORD0;
    float m_Depth : TEXCOORD1;
    float m_EyeDepth : TEXCOORD2;
};

PixelIn MainVS( VertexIn input )
{
    PixelIn output;
    float rhw = input.m_Position.w;
    float w = ( rhw > 0.0 && rhw < 1e30 ) ? 1.0 / rhw : 1.0;

    // D3D7 pixel centers sit on integers, D3D11 ones on halves.
    float2 ndc;
    ndc.x = ( input.m_Position.x + 0.5 ) * g_Target.z * 2.0 - 1.0;
    ndc.y = 1.0 - ( input.m_Position.y + 0.5 ) * g_Target.w * 2.0;

    output.m_Position = float4( ndc * w, saturate( input.m_Position.z ) * w, w );
    output.m_Diffuse = input.m_Diffuse.zyxw;
    output.m_Specular = input.m_Specular.zyxw;
    output.m_Uv = float4( input.m_Uv0, input.m_Uv1 );
    output.m_Depth = input.m_Position.z;
    output.m_EyeDepth = w;
    return output;
}

float4 Argument( int arg, float4 current, float4 diffuse, float4 specular, float4 tex )
{
    float4 value;

    int source = arg & 0x0F;
    if ( source == 0 )
        value = diffuse;
    else if ( source == 1 )
        value = current;
    else if ( source == 2 )
        value = tex;
    else if ( source == 3 )
        value = g_Factor;
    else
        value = specular;

    if ( ( arg & 0x10 ) != 0 )
        value = 1.0 - value;
    if ( ( arg & 0x20 ) != 0 )
        value = value.aaaa;

    return value;
}

float4 Combine( int op, float4 a1, float4 a2, float4 current, float4 diffuse, float4 tex )
{
    switch ( op )
    {
    case 2:
        return a1;
    case 3:
        return a2;
    case 4:
        return a1 * a2;
    case 5:
        return a1 * a2 * 2.0;
    case 6:
        return a1 * a2 * 4.0;
    case 7:
        return a1 + a2;
    case 8:
        return a1 + a2 - 0.5;
    case 9:
        return ( a1 + a2 - 0.5 ) * 2.0;
    case 10:
        return a1 - a2;
    case 11:
        return a1 + a2 - a1 * a2;
    case 12:
        return lerp( a2, a1, diffuse.a );
    case 13:
        return lerp( a2, a1, tex.a );
    case 14:
        return lerp( a2, a1, g_Factor.a );
    case 15:
        return a1 + a2 * ( 1.0 - tex.a );
    case 16:
        return lerp( a2, a1, current.a );
    default:
        return current;
    }
}

float4 RunStage( int stage, float4 current, float4 diffuse, float4 specular, float4 tex )
{
    int4 color = g_ColorOp[stage];
    int4 alpha = g_AlphaOp[stage];

    float4 c1 = Argument( color.y, current, diffuse, specular, tex );
    float4 c2 = Argument( color.z, current, diffuse, specular, tex );
    float3 rgb = Combine( color.x, c1, c2, current, diffuse, tex ).rgb;

    float a = current.a;

    if ( alpha.x > 1 )
    {
        float4 a1 = Argument( alpha.y, current, diffuse, specular, tex );
        float4 a2 = Argument( alpha.z, current, diffuse, specular, tex );
        a = Combine( alpha.x, a1, a2, current, diffuse, tex ).a;
    }

    return saturate( float4( rgb, a ) );
}

bool AlphaPasses( float a )
{
    int func = g_Flags.z;
    float ref = g_AlphaRef.x;

    switch ( func )
    {
    case 1:
        return false;
    case 2:
        return a < ref;
    case 3:
        return a == ref;
    case 4:
        return a <= ref;
    case 5:
        return a > ref;
    case 6:
        return a != ref;
    case 7:
        return a >= ref;
    default:
        return true;
    }
}

float4 MainPS( PixelIn input ) : SV_Target
{
    float4 diffuse = input.m_Diffuse;
    float4 specular = input.m_Specular;

    float4 tex0 = ( g_Flags.w & 1 ) ? g_Texture0.Sample( g_Sampler0, input.m_Uv.xy ) : float4( 1, 1, 1, 1 );
    float4 tex1 = ( g_Flags.w & 2 ) ? g_Texture1.Sample( g_Sampler1, input.m_Uv.zw ) : float4( 1, 1, 1, 1 );

    float4 current = diffuse;

    if ( g_ColorOp[0].x > 1 )
    {
        current = RunStage( 0, current, diffuse, specular, tex0 );
        if ( g_ColorOp[1].x > 1 )
            current = RunStage( 1, current, diffuse, specular, tex1 );
    }

    if ( g_Flags.y != 0 )
        current.rgb = saturate( current.rgb + specular.rgb );

    if ( !AlphaPasses( current.a ) )
        discard;

    if ( g_Flags.x != 0 )
    {
        float fog;
        int mode = (int)g_FogParams.w;
        float depth = saturate( input.m_Depth );
        float eyeDepth = max( input.m_EyeDepth, 0.0 );

        if ( mode == 1 )
            fog = exp( -g_FogParams.z * eyeDepth );
        else if ( mode == 2 )
            fog = exp( -( g_FogParams.z * eyeDepth ) * ( g_FogParams.z * eyeDepth ) );
        else if ( mode == 3 )
            fog = ( g_FogParams.y - depth ) / max( g_FogParams.y - g_FogParams.x, 1e-6 );
        else
            fog = specular.a;
        current.rgb = lerp( g_FogColor.rgb, current.rgb, saturate( fog ) );
    }
    return current;
}
)hlsl";

    // Copies a rectangle of one texture into a rectangle of a target, used for present,
    // for scaling the frame to the game resolution on lock and back on write.
    inline constexpr char PRESENT[] = R"hlsl(
cbuffer PresentConstants : register( b0 )
{
    float4 g_Rect; // destination x, y, width, height in target pixels
    float4 g_Params; // brightness, target width, target height, unused
    float4 g_Source; // source u, v, width, height in texture coordinates
};

Texture2D g_Texture : register( t0 );
SamplerState g_Sampler : register( s0 );

struct PixelIn
{
    float4 m_Position : SV_Position;
    float2 m_Uv : TEXCOORD0;
};

PixelIn MainVS( uint id : SV_VertexID )
{
    float2 corner = float2( ( id << 1 ) & 2, id & 2 ) * 0.5;
    float2 pixel = g_Rect.xy + corner * g_Rect.zw;
    PixelIn output;
    output.m_Position = float4( pixel.x / g_Params.y * 2.0 - 1.0, 1.0 - pixel.y / g_Params.z * 2.0, 0.0, 1.0 );
    output.m_Uv = g_Source.xy + corner * g_Source.zw;
    return output;
}

float4 MainPS( PixelIn input ) : SV_Target
{
    float4 color = g_Texture.Sample( g_Sampler, input.m_Uv );
    // THRASH state 2E scales the display gamma ramp.
    color.rgb = saturate( color.rgb * g_Params.x );
    return color;
}
)hlsl";

    // Same vertex stage as PRESENT. The pixel stage enlarges a small movie frame with a bicubic
    // filter from the Mitchell Netravali family, B = 1 - 2 C, so C = 0.5 is Catmull Rom, 1/3 is
    // Mitchell and 0 is the soft B spline. 'g_Params.w' is C.
    inline constexpr char MOVIE[] = R"hlsl(
cbuffer PresentConstants : register( b0 )
{
    float4 g_Rect;
    float4 g_Params; // brightness, target width, target height, bicubic C
    float4 g_Source; // source u, v, width, height in texture coordinates
};

Texture2D g_Texture : register( t0 );

struct PixelIn
{
    float4 m_Position : SV_Position;
    float2 m_Uv : TEXCOORD0;
};

float4 Weights( float t, float c )
{
    const float b = 1.0 - 2.0 * c;
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float4 w = float4(
        ( -b / 6.0 - c ) * t3 + ( b / 2.0 + 2.0 * c ) * t2 + ( -b / 2.0 - c ) * t + b / 6.0,
        ( 2.0 - 1.5 * b - c ) * t3 + ( -3.0 + 2.0 * b + c ) * t2 + ( 1.0 - b / 3.0 ),
        ( -2.0 + 1.5 * b + c ) * t3 + ( 3.0 - 2.5 * b - 2.0 * c ) * t2 + ( b / 2.0 + c ) * t + b / 6.0,
        ( b / 6.0 + c ) * t3 - c * t2 );
    return w;
}

float4 MainPS( PixelIn input ) : SV_Target
{
    uint width, height;
    g_Texture.GetDimensions( width, height );
    const float2 size = float2( width, height );
    const float2 position = input.m_Uv * size - 0.5;
    const float2 base = floor( position );
    const float2 fraction = position - base;
    const float4 wx = Weights( fraction.x, g_Params.w );
    const float4 wy = Weights( fraction.y, g_Params.w );
    // Rows outside the visible part [black bars cut by the driver] are not sampled.
    const int2 low = int2( 0, (int)round( g_Source.y * (float)height ) );
    const int2 high = int2( (int)width - 1, (int)round( ( g_Source.y + g_Source.w ) * (float)height ) - 1 );
    float3 color = 0.0;

    [unroll]
    for (int j = 0; j < 4; ++j)
    {
        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            const int2 texel = clamp( int2( base ) + int2( i - 1, j - 1 ), low, high );
            color += g_Texture.Load( int3( texel, 0 ) ).rgb * ( wx[i] * wy[j] );
        }
    }

    return float4( saturate( color * g_Params.x ), 1.0 );
}
)hlsl";

    inline constexpr char CLEAR[] = R"hlsl(
cbuffer ClearConstants : register( b0 )
{
    float4 g_Rect; // x, y, width, height in target pixels
    float4 g_Target; // target width, height, depth value, unused
    float4 g_Color;
};

struct PixelIn
{
    float4 m_Position : SV_Position;
};

PixelIn MainVS( uint id : SV_VertexID )
{
    float2 corner = float2( ( id << 1 ) & 2, id & 2 ) * 0.5;
    float2 pixel = g_Rect.xy + corner * g_Rect.zw;
    PixelIn output;
    output.m_Position = float4( pixel.x / g_Target.x * 2.0 - 1.0, 1.0 - pixel.y / g_Target.y * 2.0, g_Target.z, 1.0 );
    return output;
}

float4 MainPS( PixelIn input ) : SV_Target
{
    return g_Color;
}
)hlsl";
}
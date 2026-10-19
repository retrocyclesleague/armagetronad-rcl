#version 120
// The scene onto the screen.
//
// u_mode 0: as it is (RCL_HDR 0, the classic picture). Otherwise its display
// values are taken to light, the glow is added, the exposure applied, and a
// view transform brings it back to display values: 1 none, clipped at white;
// 2 AgX (RCL_TONEMAP 1); 3 the game's own colours, with only what goes over
// white rolled off (RCL_TONEMAP 2).
//
// AgX is Blender's own: its view transform (Blender 5.2, display sRGB, look
// None) baked into a 33 cube table by scripts/bake-agx-lut.py, shaders/agx.cube.
// The table is read through a log2 shaper of the light per channel, the range
// AgX works in (the two constants below; the bake script has the same). Read
// that way it is within half an 8-bit step of Blender on average, about 3 for
// one colour in a hundred, and up to 10 where one channel is nearly nothing
// beside a strong one (deep saturated blues and greens).
//
// Without the table, Benjamin Wrensch's "minimal AgX" (iolite engine, 2023)
// stands in: Troy Sobotka's inset and outset matrices around a polynomial fit
// of AgX's contrast curve. It is the older AgX, not Blender's: greys come out
// up to 14 steps brighter in the shadows and saturated primaries up to 60
// steps off in hue.

uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform sampler2D u_agx;        // the table: blue slices side by side, red across each, green up
uniform float u_agxSize;        // its size; 0 when there is none
uniform vec4 u_bounds;          // where the glow may be read: lowest xy, highest zw
uniform float u_mode;
uniform float u_exposure;       // a factor
uniform float u_gamma;
uniform float u_bloomStrength;
uniform float u_dither;         // a step of the screen's colours, to break up banding

varying vec2 v_uv;

const float lowest = -12.47393;
const float highest = 4.026069;

vec3 AgxTable( vec3 light )
{
    float n = u_agxSize;
    vec3 t = clamp( ( log2( max( light, vec3( exp2( lowest ) ) ) ) - lowest ) / ( highest - lowest ), 0.0, 1.0 );
    vec3 cell = t * ( n - 1.0 );
    float blue = min( floor( cell.b ), n - 2.0 );
    float between = cell.b - blue;
    // red and green by the texture's own filtering inside a slice, blue between two slices
    vec2 uv = vec2( ( blue * n + cell.r + 0.5 ) / ( n * n ), ( cell.g + 0.5 ) / n );
    vec3 a = texture2D( u_agx, uv ).rgb;
    vec3 b = texture2D( u_agx, uv + vec2( 1.0 / n, 0.0 ) ).rgb;
    return mix( a, b, between );
}

vec3 AgxContrast( vec3 x )
{
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 Agx( vec3 light )
{
    const mat3 inset = mat3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104 );
    const mat3 outset = mat3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116 );

    vec3 c = inset * light;
    c = clamp( log2( max( c, vec3( 1e-10 ) ) ), lowest, highest );
    c = ( c - lowest ) / ( highest - lowest );
    c = AgxContrast( c );
    c = outset * c;
    return c;
}

vec3 Soft( vec3 light )
{
    // up to the knee as it was; over it, eased towards white
    const float knee = 0.8;
    vec3 d = pow( light, vec3( 1.0 / u_gamma ) );
    vec3 over = max( d - knee, 0.0 );
    vec3 eased = knee + ( 1.0 - knee ) * ( 1.0 - exp( -over / ( 1.0 - knee ) ) );
    return mix( d, eased, step( knee, d ) );
}

void main()
{
    vec3 scene = texture2D( u_scene, v_uv ).rgb;
    if ( u_mode < 0.5 )
    {
        gl_FragColor = vec4( scene, 1.0 );
        return;
    }

    vec3 light = pow( max( scene, vec3( 0.0 ) ), vec3( u_gamma ) );
    if ( u_bloomStrength > 0.0 )
        light += texture2D( u_bloom, clamp( v_uv, u_bounds.xy, u_bounds.zw ) ).rgb * u_bloomStrength;
    light *= u_exposure;

    vec3 c;
    if ( u_mode < 1.5 )
        c = pow( light, vec3( 1.0 / u_gamma ) );
    else if ( u_mode < 2.5 )
        c = u_agxSize > 1.5 ? AgxTable( light ) : Agx( light );
    else
        c = Soft( light );

    float noise = fract( sin( dot( gl_FragCoord.xy, vec2( 12.9898, 78.233 ) ) ) * 43758.5453 );
    c += ( noise - 0.5 ) * u_dither;
    gl_FragColor = vec4( clamp( c, 0.0, 1.0 ), 1.0 );
}

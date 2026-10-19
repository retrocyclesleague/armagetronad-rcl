#version 120
// One step down the glow's chain: half the size, softened. The dual filter's
// downsample (Marius Bjorge, SIGGRAPH 2015): the middle four times and the
// four corners once, each a bilinear read of four texels.
//
// The first step reads the scene, which holds display values: they are taken
// to light, and only what is brighter than the threshold goes on, with a soft
// knee so that the glow fades in instead of switching on.

uniform sampler2D u_source;
uniform vec2 u_texel;       // one texel of the source
uniform vec4 u_bounds;      // where the source may be read: lowest xy, highest zw
uniform float u_prefilter;  // 1 for the first step
uniform float u_threshold;  // in light, 1 being white
uniform float u_knee;
uniform float u_gamma;

varying vec2 v_uv;

vec3 Fetch( vec2 uv )
{
    vec3 c = texture2D( u_source, clamp( uv, u_bounds.xy, u_bounds.zw ) ).rgb;
    if ( u_prefilter > 0.5 )
    {
        c = pow( max( c, vec3( 0.0 ) ), vec3( u_gamma ) );
        float bright = max( c.r, max( c.g, c.b ) );
        float soft = clamp( bright - u_threshold + u_knee, 0.0, 2.0 * u_knee );
        soft = soft * soft / ( 4.0 * u_knee + 0.0001 );
        c *= max( soft, bright - u_threshold ) / max( bright, 0.0001 );
    }
    return c;
}

void main()
{
    vec3 sum = Fetch( v_uv ) * 4.0;
    sum += Fetch( v_uv + vec2( -u_texel.x, -u_texel.y ) );
    sum += Fetch( v_uv + vec2(  u_texel.x, -u_texel.y ) );
    sum += Fetch( v_uv + vec2( -u_texel.x,  u_texel.y ) );
    sum += Fetch( v_uv + vec2(  u_texel.x,  u_texel.y ) );
    gl_FragColor = vec4( sum * 0.125, 1.0 );
}

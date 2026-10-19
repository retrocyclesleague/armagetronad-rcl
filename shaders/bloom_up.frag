#version 120
// One step up the glow's chain: the smaller level spread over this one's
// size, the dual filter's upsample (four edges once, four diagonals twice).
// It is added by blending to what this level gathered on the way down.

uniform sampler2D u_source;
uniform vec2 u_texel;       // one texel of the smaller level
uniform vec4 u_bounds;      // where it may be read: lowest xy, highest zw

varying vec2 v_uv;

vec3 Fetch( vec2 uv )
{
    return texture2D( u_source, clamp( uv, u_bounds.xy, u_bounds.zw ) ).rgb;
}

void main()
{
    vec2 h = u_texel * 0.5;
    vec3 sum = Fetch( v_uv + vec2( -u_texel.x, 0.0 ) );
    sum += Fetch( v_uv + vec2( u_texel.x, 0.0 ) );
    sum += Fetch( v_uv + vec2( 0.0, -u_texel.y ) );
    sum += Fetch( v_uv + vec2( 0.0, u_texel.y ) );
    sum += Fetch( v_uv + vec2( -h.x, -h.y ) ) * 2.0;
    sum += Fetch( v_uv + vec2(  h.x, -h.y ) ) * 2.0;
    sum += Fetch( v_uv + vec2( -h.x,  h.y ) ) * 2.0;
    sum += Fetch( v_uv + vec2(  h.x,  h.y ) ) * 2.0;
    gl_FragColor = vec4( sum / 12.0, 1.0 );
}

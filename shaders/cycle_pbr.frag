#version 120
#extension GL_ARB_shader_texture_lod : enable
// The light cycle mesh, shaded the way Blender's renders are: metal and
// roughness, light from the arena around it (a prefiltered cubemap for
// reflections, its irradiance for the rest, the split-sum table between),
// GGX with Smith and Schlick for the sun where it is a light of its own, and
// the lights in the bike's colour over white into the glow. Everything in
// light, written out as display values the way the scene holds them (the
// tone map takes them back to light).

uniform sampler2D u_normalMap;      // tangent space, OpenGL's way up
uniform sampler2D u_baseMap;        // rgb base colour (sRGB), a emissive share
uniform sampler2D u_ormMap;         // r occlusion, g roughness, b metal, a clear gloss
uniform samplerCube u_env;          // GGX-prefiltered, roughness = level / u_envLods
uniform samplerCube u_irradiance;
uniform sampler2D u_brdf;           // x NoV, y roughness -> (A, B)

uniform vec4 u_maps;                // which there are: normal, base, orm, emissive share
uniform float u_envMaps;            // 1 the cubemaps, 0 the sky worked out below
uniform float u_envLods;            // the roughness of the last level the bake made, in levels
uniform float u_envTop;             // the last level there is
uniform float u_brdfMap;
uniform vec2 u_envTurn;             // cos, sin: the arena turned into the cubemaps' frame

uniform vec3 u_eye;
uniform vec3 u_sun;                 // towards the sun
uniform vec3 u_sunLight;            // the sun as a light of its own; 0 where the cubemaps hold it
uniform vec3 u_sky;                 // overhead, at the horizon, the floor: without cubemaps
uniform vec3 u_horizon;
uniform vec3 u_ground;

uniform vec3 u_base;                // the material, where there are no maps
uniform vec4 u_surface;             // roughness, metal, clear gloss, emissive
uniform vec3 u_light;               // the bike's colour as light, glow and all

varying vec3 v_pos;
varying vec3 v_normal;
varying vec4 v_tangent;
varying vec2 v_uv;

const float pi = 3.14159265;

vec3 ToCube( vec3 d )
{
    return vec3( u_envTurn.x * d.x - u_envTurn.y * d.y, u_envTurn.y * d.x + u_envTurn.x * d.y, d.z );
}

// without cubemaps: the sky towards the top, the floor under the horizon
vec3 Sky( vec3 d )
{
    if ( d.z > 0.0 )
        return mix( u_horizon, u_sky, pow( d.z, 0.6 ) );
    return mix( u_horizon, u_ground, min( 1.0, -d.z * 8.0 ) );
}

vec3 Reflected( vec3 r, float rough )
{
    if ( u_envMaps < 0.5 )
        return mix( Sky( r ), 0.5 * ( u_sky + u_ground ), rough * rough );
    float level = min( rough * u_envLods, u_envTop );
#ifdef GL_ARB_shader_texture_lod
    return textureCubeLod( u_env, ToCube( r ), level ).rgb;
#else
    // a bias on the level the screen picks, not the level itself
    return textureCube( u_env, ToCube( r ), level ).rgb;
#endif
}

vec3 Irradiance( vec3 n )
{
    if ( u_envMaps < 0.5 )
        return mix( u_ground, mix( u_horizon, u_sky, 0.5 ), n.z * 0.5 + 0.5 );
    return textureCube( u_irradiance, ToCube( n ) ).rgb;
}

// the split-sum table, or Karis's fit of it (Unreal, 2014)
vec2 Brdf( float nv, float rough )
{
    if ( u_brdfMap > 0.5 )
        return texture2D( u_brdf, vec2( nv, rough ) ).rg;
    vec4 r = rough * vec4( -1.0, -0.0275, -0.572, 0.022 ) + vec4( 1.0, 0.0425, 1.04, -0.04 );
    float a = min( r.x * r.x, exp2( -9.28 * nv ) ) * r.x + r.y;
    return vec2( -1.04, 1.04 ) * a + r.zw;
}

// the sun as a light: GGX, Smith's shadowing (Schlick's form), Schlick's Fresnel
vec3 Sun( vec3 n, vec3 v, vec3 base, float rough, float metal )
{
    float nl = dot( n, u_sun );
    if ( nl <= 0.0 )
        return vec3( 0.0 );
    vec3 h = normalize( u_sun + v );
    float nv = max( dot( n, v ), 0.0001 );
    float nh = max( dot( n, h ), 0.0 );
    float vh = max( dot( v, h ), 0.0 );
    float a = max( rough * rough, 0.002 );
    float a2 = a * a;
    float d = nh * nh * ( a2 - 1.0 ) + 1.0;
    float ggx = a2 / ( pi * d * d );
    float k = ( rough + 1.0 ) * ( rough + 1.0 ) / 8.0;
    float smith = ( nv / ( nv * ( 1.0 - k ) + k ) ) * ( nl / ( nl * ( 1.0 - k ) + k ) );
    vec3 f0 = mix( vec3( 0.04 ), base, metal );
    vec3 fresnel = f0 + ( 1.0 - f0 ) * pow( 1.0 - vh, 5.0 );
    vec3 specular = ggx * smith * fresnel / ( 4.0 * nv * nl + 0.0001 );
    vec3 diffuse = ( 1.0 - fresnel ) * ( 1.0 - metal ) * base / pi;
    return ( diffuse + specular ) * nl * u_sunLight;
}

vec3 FromSrgb( vec3 c )
{
    return mix( c / 12.92, pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), step( 0.04045, c ) );
}

void main()
{
    vec3 normal = normalize( v_normal );
    bool front = gl_FrontFacing;
    if ( !front )
        normal = -normal;
    vec3 n = normal;
    if ( u_maps.x > 0.5 && front )
    {
        vec3 t = normalize( v_tangent.xyz - normal * dot( normal, v_tangent.xyz ) );
        vec3 b = cross( normal, t ) * v_tangent.w;
        vec3 m = texture2D( u_normalMap, v_uv ).xyz * 2.0 - 1.0;
        n = normalize( t * m.x + b * m.y + normal * m.z );
    }

    vec3 base = u_base;
    float rough = u_surface.x, metal = u_surface.y, gloss = u_surface.z, emit = u_surface.w, occlusion = 1.0;
    if ( u_maps.y > 0.5 || u_maps.w > 0.5 )
    {
        vec4 b = texture2D( u_baseMap, v_uv );
        if ( u_maps.y > 0.5 )
            base = FromSrgb( b.rgb );
        if ( u_maps.w > 0.5 )
            emit = b.a;
    }
    if ( u_maps.z > 0.5 )
    {
        vec4 o = texture2D( u_ormMap, v_uv );
        occlusion = o.r;
        rough = o.g;
        metal = o.b;
        gloss = o.a;
    }
    rough = clamp( rough, 0.03, 1.0 );

    vec3 v = normalize( u_eye - v_pos );
    float nv = max( dot( n, v ), 0.0001 );
    vec3 r = reflect( -v, n );
    vec3 f0 = mix( vec3( 0.04 ), base, metal );
    vec2 ab = Brdf( nv, rough );
    vec3 colour = ( Irradiance( n ) * base * ( 1.0 - metal ) + Reflected( r, rough ) * ( f0 * ab.x + ab.y ) ) * occlusion;

    // a clear coat over it: a sharp reflection that takes its share off what is under it
    if ( gloss > 0.0 )
    {
        vec2 c = Brdf( nv, 0.03 );
        float coat = gloss * ( 0.04 * c.x + c.y );
        colour = colour * ( 1.0 - coat ) + Reflected( r, 0.03 ) * coat * occlusion;
    }

    colour += Sun( n, v, base, rough, metal );
    colour += emit * u_light;
    gl_FragColor = vec4( pow( max( colour, vec3( 0.0 ) ), vec3( 1.0 / 2.2 ) ), 1.0 );
}

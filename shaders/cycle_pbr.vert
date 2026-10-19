#version 120
// The light cycle mesh (gCycleMesh.cpp). Placed exactly as the fixed-function
// drawing places things; shaded in the arena's frame, which u_model gives
// without the floor's mirror (the mirror image is the real cycle, seen from
// under the floor).

uniform mat4 u_model;       // the object to the arena

varying vec3 v_pos;
varying vec3 v_normal;
varying vec4 v_tangent;     // w: which way the bitangent goes
varying vec2 v_uv;

void main()
{
    vec4 world = u_model * gl_Vertex;
    v_pos = world.xyz;
    // turns and one size for all axes: the normal goes as the tangent does
    mat3 turn = mat3( u_model[0].xyz, u_model[1].xyz, u_model[2].xyz );
    v_normal = turn * gl_Normal;
    v_tangent = vec4( turn * gl_MultiTexCoord1.xyz, gl_MultiTexCoord1.w );
    v_uv = gl_MultiTexCoord0.xy;
    gl_Position = ftransform();
}

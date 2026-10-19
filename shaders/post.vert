#version 120
// A picture drawn over the viewport (rPost.cpp): the corners come in clip
// space, the part of the source pictures to read in texture coordinates.

varying vec2 v_uv;

void main()
{
    v_uv = gl_MultiTexCoord0.xy;
    gl_Position = vec4( gl_Vertex.xy, 0.0, 1.0 );
}

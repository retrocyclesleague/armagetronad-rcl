#version 120
// The scene onto the screen as it is (rPost). The view transform and the glow
// come with the half float scene.

uniform sampler2D u_scene;

varying vec2 v_uv;

void main()
{
    gl_FragColor = vec4( texture2D( u_scene, v_uv ).rgb, 1.0 );
}

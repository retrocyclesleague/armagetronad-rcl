#version 120
// Things that give off light: their colour scaled so that its light (the
// display value to the power 2.2) comes out times the intensity asked for,
// above white into the glow. Their see-through stays as it was.

uniform float u_scale;

varying vec4 v_colour;

void main()
{
    gl_FragColor = vec4( v_colour.rgb * u_scale, v_colour.a );
}

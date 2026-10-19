#version 120
// Things that give off light (rEmissive): placed exactly as the fixed-function
// drawing places them, so they meet the rest of the scene in depth as before.

varying vec4 v_colour;

void main()
{
    v_colour = gl_Color;
    gl_Position = ftransform();
}

#version 120
uniform sampler2D sourceTexture;
uniform vec2 signalSize;
uniform float maskStrength;
uniform float scanlineStrength;
varying vec2 textureCoordinate;

void main() {
    vec3 color = texture2D(sourceTexture, textureCoordinate).rgb;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float rowPhase = fract(textureCoordinate.y * signalSize.y);
    float trough = 0.5 - 0.5 * cos(rowPhase * 6.28318530718);
    float beamLoss = mix(0.18, 0.09, clamp(sqrt(max(luma, 0.0)), 0.0, 1.0));
    color *= 1.0 - scanlineStrength * beamLoss * trough;

    float phase = mod(floor(gl_FragCoord.x), 3.0);
    vec3 mask = vec3(1.0 - maskStrength);
    if (phase < 0.5) mask.r = 1.0 + 2.0 * maskStrength;
    else if (phase < 1.5) mask.g = 1.0 + 2.0 * maskStrength;
    else mask.b = 1.0 + 2.0 * maskStrength;
    gl_FragColor = vec4(max(color * mask, 0.0), 1.0);
}

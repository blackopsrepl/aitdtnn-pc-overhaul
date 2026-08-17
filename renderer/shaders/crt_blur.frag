#version 120
uniform sampler2D sourceTexture;
uniform vec2 inverseSize;
uniform vec2 blurDirection;
uniform float extractHighlights;
varying vec2 textureCoordinate;

vec3 sourceSample(vec2 coordinate) {
    vec3 color = texture2D(sourceTexture, coordinate).rgb;
    if (extractHighlights > 0.5) {
        float peak = max(max(color.r, color.g), color.b);
        float weight = smoothstep(0.25, 0.85, peak);
        color *= weight;
    }
    return color;
}

void main() {
    vec2 stepVector = inverseSize * blurDirection;
    vec3 color = sourceSample(textureCoordinate) * 0.2270270270;
    color += sourceSample(textureCoordinate + stepVector * 1.3846153846) * 0.3162162162;
    color += sourceSample(textureCoordinate - stepVector * 1.3846153846) * 0.3162162162;
    color += sourceSample(textureCoordinate + stepVector * 3.2307692308) * 0.0702702703;
    color += sourceSample(textureCoordinate - stepVector * 3.2307692308) * 0.0702702703;
    gl_FragColor = vec4(color, 1.0);
}

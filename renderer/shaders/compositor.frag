#version 120
uniform sampler2D sourceTexture;
uniform vec2 inverseSize;
uniform float debandAmount;
uniform float ditherAmount;
varying vec2 textureCoordinate;

float hashNoise(vec2 position) {
    return fract(52.9829189 * fract(dot(position, vec2(0.06711056, 0.00583715))));
}

float blueNoise(vec2 position) {
    float center = hashNoise(position);
    float lowFrequency = (hashNoise(position + vec2(1.0, 0.0)) +
                          hashNoise(position - vec2(1.0, 0.0)) +
                          hashNoise(position + vec2(0.0, 1.0)) +
                          hashNoise(position - vec2(0.0, 1.0))) * 0.25;
    return clamp((center - lowFrequency) * 0.625, -0.5, 0.5);
}

void main() {
    vec3 center = texture2D(sourceTexture, textureCoordinate).rgb;
    vec3 leftColor = texture2D(sourceTexture, textureCoordinate - vec2(inverseSize.x, 0.0)).rgb;
    vec3 rightColor = texture2D(sourceTexture, textureCoordinate + vec2(inverseSize.x, 0.0)).rgb;
    vec3 downColor = texture2D(sourceTexture, textureCoordinate - vec2(0.0, inverseSize.y)).rgb;
    vec3 upColor = texture2D(sourceTexture, textureCoordinate + vec2(0.0, inverseSize.y)).rgb;
    vec3 averageColor = (leftColor + rightColor + downColor + upColor) * 0.25;
    vec3 localRange = max(max(abs(center - leftColor), abs(center - rightColor)),
                          max(abs(center - downColor), abs(center - upColor)));
    float difference = max(max(localRange.r, localRange.g), localRange.b);
    float gradientWeight = 1.0 - smoothstep(1.0 / 255.0, 4.0 / 255.0, difference);
    center = mix(center, averageColor, gradientWeight * 0.18 * debandAmount);
    center += blueNoise(gl_FragCoord.xy) * (0.75 / 255.0) * ditherAmount;
    gl_FragColor = vec4(clamp(center, 0.0, 1.0), 1.0);
}

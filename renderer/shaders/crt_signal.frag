#version 120
uniform sampler2D sourceTexture;
uniform vec2 inverseSize;
uniform float debandAmount;
varying vec2 textureCoordinate;

float toLinear(float encoded) {
    return encoded <= 0.04045 ? encoded / 12.92
                              : pow((encoded + 0.055) / 1.055, 2.4);
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
    float weight = 1.0 - smoothstep(1.0 / 255.0, 4.0 / 255.0, difference);
    center = clamp(mix(center, averageColor, weight * 0.18 * debandAmount), 0.0, 1.0);
    gl_FragColor = vec4(toLinear(center.r), toLinear(center.g), toLinear(center.b), 1.0);
}

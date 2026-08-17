#version 120
uniform sampler2D sourceTexture;
uniform sampler2D blurTexture;
uniform float bloomStrength;
uniform float halationStrength;
uniform float ditherAmount;
varying vec2 textureCoordinate;

float toSrgb(float linearValue) {
    linearValue = max(linearValue, 0.0);
    return linearValue <= 0.0031308 ? linearValue * 12.92
                                   : 1.055 * pow(linearValue, 1.0 / 2.4) - 0.055;
}

float hashNoise(vec2 position) {
    return fract(52.9829189 * fract(dot(position, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec3 response = texture2D(sourceTexture, textureCoordinate).rgb;
    vec3 blurred = texture2D(blurTexture, textureCoordinate).rgb;
    vec3 halo = max(blurred - response * 0.35, 0.0);
    vec3 linearColor = response + blurred * bloomStrength + halo * halationStrength;
    vec3 encoded = vec3(toSrgb(linearColor.r), toSrgb(linearColor.g), toSrgb(linearColor.b));
    float noise = hashNoise(gl_FragCoord.xy) - 0.5;
    encoded += noise * (0.5 / 255.0) * ditherAmount;
    gl_FragColor = vec4(clamp(encoded, 0.0, 1.0), 1.0);
}

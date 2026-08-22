// Neutral CRT shader programs and their runtime configuration.
// Read this fragment in the order listed by the parent .cpp file. Names declared in
// earlier fragments are intentionally visible here because all fragments form one unit.

bool create_crt_programs() {
    if (!g_config.crt_enabled) {
        crt_ready = false;
        return true;
    }
    static constexpr char vertex_source[] = R"GLSL(
#version 120
varying vec2 textureCoordinate;
void main() {
    gl_Position = gl_Vertex;
    textureCoordinate = gl_MultiTexCoord0.xy;
}
)GLSL";
    static constexpr char signal_fragment[] = R"GLSL(
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
)GLSL";
    static constexpr char response_fragment[] = R"GLSL(
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
)GLSL";
    static constexpr char blur_fragment[] = R"GLSL(
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
)GLSL";
    static constexpr char present_fragment[] = R"GLSL(
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
)GLSL";

    std::string vertex_override;
    std::string signal_override;
    std::string response_override;
    std::string blur_override;
    std::string present_override;
    const char* selected_vertex = vertex_source;
    const char* selected_signal = signal_fragment;
    const char* selected_response = response_fragment;
    const char* selected_blur = blur_fragment;
    const char* selected_present = present_fragment;
    if (g_config.development_hot_reload) {
        vertex_override = read_shader_override("compositor.vert");
        signal_override = read_shader_override("crt_signal.frag");
        response_override = read_shader_override("crt_response.frag");
        blur_override = read_shader_override("crt_blur.frag");
        present_override = read_shader_override("crt_present.frag");
        if (!vertex_override.empty()) selected_vertex = vertex_override.c_str();
        if (!signal_override.empty()) selected_signal = signal_override.c_str();
        if (!response_override.empty()) selected_response = response_override.c_str();
        if (!blur_override.empty()) selected_blur = blur_override.c_str();
        if (!present_override.empty()) selected_present = present_override.c_str();
    }

    const GLuint signal = create_linked_program("CRT signal", selected_vertex, selected_signal);
    const GLuint response = create_linked_program("CRT response", selected_vertex, selected_response);
    const GLuint blur = create_linked_program("CRT blur", selected_vertex, selected_blur);
    const GLuint present = create_linked_program("CRT present", selected_vertex, selected_present);
    if (!signal || !response || !blur || !present) {
        if (signal) delete_program(signal);
        if (response) delete_program(response);
        if (blur) delete_program(blur);
        if (present) delete_program(present);
        return false;
    }

    const GLint signal_source = get_uniform_location(signal, "sourceTexture");
    const GLint signal_inverse = get_uniform_location(signal, "inverseSize");
    const GLint signal_deband = get_uniform_location(signal, "debandAmount");
    const GLint response_source = get_uniform_location(response, "sourceTexture");
    const GLint response_size = get_uniform_location(response, "signalSize");
    const GLint response_mask = get_uniform_location(response, "maskStrength");
    const GLint response_scanline = get_uniform_location(response, "scanlineStrength");
    const GLint blur_source = get_uniform_location(blur, "sourceTexture");
    const GLint blur_inverse = get_uniform_location(blur, "inverseSize");
    const GLint blur_direction = get_uniform_location(blur, "blurDirection");
    const GLint blur_extract = get_uniform_location(blur, "extractHighlights");
    const GLint present_source = get_uniform_location(present, "sourceTexture");
    const GLint present_blur = get_uniform_location(present, "blurTexture");
    const GLint present_bloom = get_uniform_location(present, "bloomStrength");
    const GLint present_halation = get_uniform_location(present, "halationStrength");
    const GLint present_dither = get_uniform_location(present, "ditherAmount");
    if (signal_source < 0 || signal_inverse < 0 || signal_deband < 0 ||
        response_source < 0 || response_size < 0 || response_mask < 0 ||
        response_scanline < 0 || blur_source < 0 || blur_inverse < 0 ||
        blur_direction < 0 || blur_extract < 0 || present_source < 0 ||
        present_blur < 0 || present_bloom < 0 || present_halation < 0 ||
        present_dither < 0) {
        log_line("CRT shader link omitted one or more required uniforms");
        delete_program(signal);
        delete_program(response);
        delete_program(blur);
        delete_program(present);
        return false;
    }

    if (crt_signal_program) delete_program(crt_signal_program);
    if (crt_response_program) delete_program(crt_response_program);
    if (crt_blur_program) delete_program(crt_blur_program);
    if (crt_present_program) delete_program(crt_present_program);
    crt_signal_program = signal;
    crt_response_program = response;
    crt_blur_program = blur;
    crt_present_program = present;
    crt_signal_source_uniform = signal_source;
    crt_signal_inverse_size_uniform = signal_inverse;
    crt_signal_deband_uniform = signal_deband;
    crt_response_source_uniform = response_source;
    crt_response_signal_size_uniform = response_size;
    crt_response_mask_uniform = response_mask;
    crt_response_scanline_uniform = response_scanline;
    crt_blur_source_uniform = blur_source;
    crt_blur_inverse_size_uniform = blur_inverse;
    crt_blur_direction_uniform = blur_direction;
    crt_blur_extract_uniform = blur_extract;
    crt_present_source_uniform = present_source;
    crt_present_blur_uniform = present_blur;
    crt_present_bloom_uniform = present_bloom;
    crt_present_halation_uniform = present_halation;
    crt_present_dither_uniform = present_dither;
    log_line("CRT shaders ready signal=%s response=%s blur=%s present=%s",
             signal_override.empty() ? "embedded" : "override",
             response_override.empty() ? "embedded" : "override",
             blur_override.empty() ? "embedded" : "override",
             present_override.empty() ? "embedded" : "override");
    return true;
}

#version 430 core

in vec3 vNormal;
in vec4 vColorA;
in vec4 vColorB;
in float vGradientT;

out vec4 oColor;

void main()
{
    vec3 baseColor = mix(vColorA.rgb, vColorB.rgb, clamp(vGradientT, 0.0, 1.0));
    vec3 lightDirection = normalize(vec3(0.5, 0.8, 0.2));
    float diffuse = max(dot(normalize(vNormal), lightDirection), 0.3);
    oColor = vec4(baseColor * diffuse, 1.0);
}


#version 430 core

in vec3 vNormal;
in vec4 vColor;

out vec4 oColor;

void main()
{
    vec3 lightDirection = normalize(vec3(0.4, 0.9, 0.3));
    float diffuse = max(dot(normalize(vNormal), lightDirection), 0.25);
    vec3 color = vColor.rgb * diffuse;
    oColor = vec4(color, vColor.a);
}


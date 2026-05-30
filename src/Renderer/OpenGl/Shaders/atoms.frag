#version 430 core

in vec3 vNormal;
in vec4 vColor;
in vec3 vWorldPos;

out vec4 oColor;

void main()
{
	vec3 N = normalize(vNormal);

	vec3 key = normalize(vec3(0.6, 0.8, 0.5));
	vec3 fill = normalize(vec3(-0.7, 0.3, 0.2));
	vec3 backLight = normalize(vec3(0.0, -0.4, -0.8));

	float dKey = max(dot(N, key), 0.0) * 0.55;
	float dFill = max(dot(N, fill), 0.0) * 0.25;
	float dBack = max(dot(N, backLight), 0.0) * 0.12;
	float ambient = 0.18;

	float intensity = ambient + dKey + dFill + dBack;
	oColor = vec4(vColor.rgb * intensity, vColor.a);
}

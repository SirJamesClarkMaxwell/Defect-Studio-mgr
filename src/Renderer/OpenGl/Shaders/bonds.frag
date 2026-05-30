#version 430 core

in vec3 vNormal;
in vec4 vColorA;
in vec4 vColorB;
in float vGradientT;

out vec4 oColor;

uniform vec3 u_KeyDirection;
uniform vec3 u_FillDirection;
uniform vec3 u_BackDirection;
uniform float u_AmbientIntensity;
uniform float u_KeyIntensity;
uniform float u_FillIntensity;
uniform float u_BackIntensity;
uniform int u_TwoSidedLighting;

float ComputeDiffuse(vec3 normalVector, vec3 lightDirection)
{
	float value = dot(normalVector, normalize(lightDirection));
	if (u_TwoSidedLighting == 1)
		return abs(value);
	return max(value, 0.0);
}

void main()
{
	vec3 N = normalize(vNormal);
	vec3 baseColor = mix(vColorA.rgb, vColorB.rgb, clamp(vGradientT, 0.0, 1.0));
	float dKey = ComputeDiffuse(N, u_KeyDirection) * u_KeyIntensity;
	float dFill = ComputeDiffuse(N, u_FillDirection) * u_FillIntensity;
	float dBack = ComputeDiffuse(N, u_BackDirection) * u_BackIntensity;
	float intensity = u_AmbientIntensity + dKey + dFill + dBack;
	oColor = vec4(baseColor * intensity, 1.0);
}

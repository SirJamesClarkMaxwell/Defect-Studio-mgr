#version 430 core

in vec3 vNormal;
in float vSign;

out vec4 oColor;

uniform vec3 u_KeyDirection;
uniform vec3 u_FillDirection;
uniform vec3 u_BackDirection;
uniform float u_AmbientIntensity;
uniform float u_KeyIntensity;
uniform float u_FillIntensity;
uniform float u_BackIntensity;
uniform int u_TwoSidedLighting;
uniform vec3 u_PositiveLobeColor;
uniform vec3 u_NegativeLobeColor;
uniform float u_LobeAlpha;

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
	float dKey = ComputeDiffuse(N, u_KeyDirection) * u_KeyIntensity;
	float dFill = ComputeDiffuse(N, u_FillDirection) * u_FillIntensity;
	float dBack = ComputeDiffuse(N, u_BackDirection) * u_BackIntensity;
	float intensity = u_AmbientIntensity + dKey + dFill + dBack;
	vec3 baseColor = vSign > 0.0 ? u_PositiveLobeColor : u_NegativeLobeColor;
	oColor = vec4(baseColor * intensity, u_LobeAlpha);
}

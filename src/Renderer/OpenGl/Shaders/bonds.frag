#version 430 core

in vec3 vNormal;
in vec4 vColorA;
in vec4 vColorB;
in float vGradientT;
in vec3 vWorldPos;

out vec4 oColor;

uniform vec3 u_KeyDirection;
uniform vec3 u_FillDirection;
uniform vec3 u_BackDirection;
uniform float u_AmbientIntensity;
uniform float u_KeyIntensity;
uniform float u_FillIntensity;
uniform float u_BackIntensity;
uniform int u_TwoSidedLighting;
uniform vec3 u_CameraPosition;
uniform float u_SpecularIntensity;
uniform float u_Shininess;

float ComputeDiffuse(vec3 normalVector, vec3 lightDirection)
{
	float value = dot(normalVector, normalize(lightDirection));
	if (u_TwoSidedLighting == 1)
		return abs(value);
	return max(value, 0.0);
}

float ComputeSpecular(vec3 normalVector, vec3 lightDirection, vec3 viewDirection)
{
	vec3 halfVector = normalize(normalize(lightDirection) + viewDirection);
	float value = dot(normalVector, halfVector);
	if (u_TwoSidedLighting == 1)
		value = abs(value);
	else
		value = max(value, 0.0);
	return pow(value, u_Shininess);
}

void main()
{
	vec3 N = normalize(vNormal);
	vec3 viewDir = normalize(u_CameraPosition - vWorldPos);
	vec3 baseColor = mix(vColorA.rgb, vColorB.rgb, clamp(vGradientT, 0.0, 1.0));
	float dKey = ComputeDiffuse(N, u_KeyDirection) * u_KeyIntensity;
	float dFill = ComputeDiffuse(N, u_FillDirection) * u_FillIntensity;
	float dBack = ComputeDiffuse(N, u_BackDirection) * u_BackIntensity;
	float intensity = min(u_AmbientIntensity + dKey + dFill + dBack, 1.0);
	float specular = ComputeSpecular(N, u_KeyDirection, viewDir) * u_SpecularIntensity;
	oColor = vec4(clamp(baseColor * intensity + vec3(specular), 0.0, 1.0), 1.0);
}

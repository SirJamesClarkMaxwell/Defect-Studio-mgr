#version 430 core

in vec3 vNormal;
in vec4 vColor;
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
uniform float u_Saturation;
uniform float u_SpecularScale;

vec3 ApplySaturation(vec3 color)
{
	float luma = dot(color, vec3(0.299, 0.587, 0.114));
	return mix(vec3(luma), color, u_Saturation);
}

float Diffuse(vec3 normalVector, vec3 lightDirection)
{
	float value = dot(normalVector, normalize(lightDirection));
	return u_TwoSidedLighting == 1 ? abs(value) : max(value, 0.0);
}

float Specular(vec3 normalVector, vec3 lightDirection, vec3 viewDirection)
{
	vec3 halfVector = normalize(normalize(lightDirection) + viewDirection);
	float value = dot(normalVector, halfVector);
	value = u_TwoSidedLighting == 1 ? abs(value) : max(value, 0.0);
	return pow(value, u_Shininess);
}

void main()
{
	vec3 normal = normalize(vNormal);
	vec3 viewDirection = normalize(u_CameraPosition - vWorldPos);
	float intensity = min(u_AmbientIntensity + Diffuse(normal, u_KeyDirection) * u_KeyIntensity
		+ Diffuse(normal, u_FillDirection) * u_FillIntensity
		+ Diffuse(normal, u_BackDirection) * u_BackIntensity, 1.0);
	float specular = Specular(normal, u_KeyDirection, viewDirection) * u_SpecularIntensity * u_SpecularScale;
	oColor = vec4(clamp(ApplySaturation(vColor.rgb) * intensity + vec3(specular), 0.0, 1.0), vColor.a);
}

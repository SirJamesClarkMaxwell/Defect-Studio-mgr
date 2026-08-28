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
uniform float u_Saturation;
// 1.0 for normal bonds, ~0.25 for SceneArrow shafts/heads (see renderSceneArrows) - an annotation
// arrow is a fixed flat color, not atom/bond material, so it shouldn't carry the same shiny
// highlight (docs/scene_arrow_rework_plan_corrected.md Step 8).
uniform float u_SpecularScale;

vec3 ApplySaturation(vec3 color)
{
	float luma = dot(color, vec3(0.299, 0.587, 0.114));
	return mix(vec3(luma), color, u_Saturation);
}

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
	vec3 baseColor = ApplySaturation(mix(vColorA.rgb, vColorB.rgb, clamp(vGradientT, 0.0, 1.0)));
	float dKey = ComputeDiffuse(N, u_KeyDirection) * u_KeyIntensity;
	float dFill = ComputeDiffuse(N, u_FillDirection) * u_FillIntensity;
	float dBack = ComputeDiffuse(N, u_BackDirection) * u_BackIntensity;
	float intensity = min(u_AmbientIntensity + dKey + dFill + dBack, 1.0);
	float specular = ComputeSpecular(N, u_KeyDirection, viewDir) * u_SpecularIntensity * u_SpecularScale;
	float alpha = mix(vColorA.a, vColorB.a, clamp(vGradientT, 0.0, 1.0));
	oColor = vec4(clamp(baseColor * intensity + vec3(specular), 0.0, 1.0), alpha);
}

#version 430 core

in vec3 vNormal;
in float vSign;
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
uniform vec3 u_PositiveLobeColor;
uniform vec3 u_NegativeLobeColor;
uniform float u_LobeAlpha;
uniform vec3 u_CameraPosition;
uniform float u_SpecularIntensity;
uniform float u_Shininess;
uniform float u_RimIntensity;
uniform float u_RimPower;
uniform float u_Saturation;
uniform int u_OutlineMode;
uniform vec4 u_OutlineColor;

// Zero for ordinary isosurfaces. Vacancy discs keep their flat colour and spherical depth.
uniform float u_VacancyRadius;
uniform vec3 u_VacancyCenter;
uniform vec3 u_VacancyNormal;
uniform int u_VacancyPerspective;
uniform mat4 u_ViewProjection;

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
	// Assign on every path, including the outline return, as required by GLSL.
	gl_FragDepth = gl_FragCoord.z;
	if (u_VacancyRadius > 0.0)
	{
		vec3 towardCamera = u_VacancyPerspective == 1 ?
			normalize(u_CameraPosition - vWorldPos) : u_VacancyNormal;
		vec3 delta = vWorldPos - u_VacancyCenter;
		float b = dot(delta, towardCamera);
		float height = -b + sqrt(max(b * b + u_VacancyRadius * u_VacancyRadius -
			dot(delta, delta), 0.0));
		vec4 clip = u_ViewProjection * vec4(vWorldPos + towardCamera * height, 1.0);
		gl_FragDepth = gl_DepthRange.near + gl_DepthRange.diff * (clip.z / clip.w * 0.5 + 0.5);
	}
	if (u_OutlineMode == 1)
	{
		oColor = u_OutlineColor;
		return;
	}
	vec3 N = normalize(vNormal);
	vec3 viewDir = normalize(u_CameraPosition - vWorldPos);
	float dKey = ComputeDiffuse(N, u_KeyDirection) * u_KeyIntensity;
	float dFill = ComputeDiffuse(N, u_FillDirection) * u_FillIntensity;
	float dBack = ComputeDiffuse(N, u_BackDirection) * u_BackIntensity;
	float intensity = min(u_AmbientIntensity + dKey + dFill + dBack, 1.0);
	float specular = ComputeSpecular(N, u_KeyDirection, viewDir) * u_SpecularIntensity;
	vec3 baseColor = ApplySaturation(vSign > 0.0 ? u_PositiveLobeColor : u_NegativeLobeColor);
	// Fresnel rim - grazing-angle glow tinted by the lobe's own color, the "misty" edge look common
	// in electron-density isosurface renders (VESTA/VMD-style), not just a flat-shaded blob.
	float facing = u_TwoSidedLighting == 1 ? abs(dot(N, viewDir)) : max(dot(N, viewDir), 0.0);
	float rim = pow(1.0 - facing, u_RimPower) * u_RimIntensity;
	vec3 finalColor = clamp(baseColor * intensity + vec3(specular) + baseColor * rim, 0.0, 1.0);
	oColor = vec4(finalColor, clamp(u_LobeAlpha + rim, 0.0, 1.0));
}

#include "Core/dspch.hpp"

#include "App/Serialization/YamlRendererConfigSection.hpp"

#include <array>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace DefectStudio::ConfigYaml
{
	void EmitColor(YAML::Emitter &emitter, const std::array<float, 4> &color);
	void EmitVec3(YAML::Emitter &emitter, const std::array<float, 3> &vector);
	void EmitFloatList(YAML::Emitter &emitter, const std::vector<float> &values);

	void EmitRendererConfig(YAML::Emitter &out, const RendererConfig &renderer)
	{
		out << YAML::Key << "renderer" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "background" << YAML::Value;
		EmitColor(out, renderer.backgroundColor);
		out << YAML::Key << "orbit_sensitivity" << YAML::Value << renderer.orbitSensitivity;
		out << YAML::Key << "pan_sensitivity" << YAML::Value << renderer.panSensitivity;
		out << YAML::Key << "zoom_sensitivity" << YAML::Value << renderer.zoomSensitivity;
		out << YAML::Key << "rotation_speed" << YAML::Value << renderer.rotationSpeed;
		out << YAML::Key << "focus_selected_atom_distance" << YAML::Value << renderer.focusSelectedAtomDistance;
		out << YAML::Key << "focus_selected_atom_transition_seconds" << YAML::Value << renderer.focusSelectedAtomTransitionSeconds;
		out << YAML::Key << "focus_selected_atom_respect_atom_radius" << YAML::Value << renderer.focusSelectedAtomRespectAtomRadius;
		out << YAML::Key << "focus_selected_atom_radius_multiplier" << YAML::Value << renderer.focusSelectedAtomRadiusMultiplier;
		out << YAML::Key << "invert_zoom" << YAML::Value << renderer.invertZoom;
		out << YAML::Key << "touchpad_navigation" << YAML::Value << renderer.touchpadNavigation;
		out << YAML::Key << "default_projection" << YAML::Value << renderer.defaultProjection;
		out << YAML::Key << "auto_apply_default_view_on_open" << YAML::Value << renderer.autoApplyDefaultViewOnOpen;
		out << YAML::Key << "lighting" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "ambient" << YAML::Value << renderer.lighting.ambientIntensity;
		out << YAML::Key << "key" << YAML::Value << renderer.lighting.keyIntensity;
		out << YAML::Key << "fill" << YAML::Value << renderer.lighting.fillIntensity;
		out << YAML::Key << "back" << YAML::Value << renderer.lighting.backIntensity;
		out << YAML::Key << "two_sided" << YAML::Value << renderer.lighting.twoSided;
		out << YAML::Key << "key_direction" << YAML::Value;
		EmitVec3(out, renderer.lighting.keyDirection);
		out << YAML::Key << "fill_direction" << YAML::Value;
		EmitVec3(out, renderer.lighting.fillDirection);
		out << YAML::Key << "back_direction" << YAML::Value;
		EmitVec3(out, renderer.lighting.backDirection);
		out << YAML::EndMap;
		out << YAML::Key << "viewport" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "axis_button_size" << YAML::Value << renderer.viewport.axisButtonSize;
		out << YAML::Key << "icon_button_size" << YAML::Value << renderer.viewport.iconButtonSize;
		out << YAML::EndMap;
		out << YAML::Key << "toolbar_wheel" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "rotation_step_delta" << YAML::Value << renderer.toolbarWheel.rotationStepDelta;
		out << YAML::Key << "zoom_step_delta" << YAML::Value << renderer.toolbarWheel.zoomStepDelta;
		out << YAML::Key << "ctrl_presets" << YAML::Value;
		EmitFloatList(out, renderer.toolbarWheel.ctrlPresetValues);
		out << YAML::EndMap;
		out << YAML::Key << "grid" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "auto_fit_to_structure_bounds" << YAML::Value << renderer.grid.autoFitToStructureBounds;
		out << YAML::Key << "padding_percent" << YAML::Value << renderer.grid.paddingPercent;
		out << YAML::Key << "spacing" << YAML::Value << renderer.grid.spacing;
		out << YAML::Key << "plane_z" << YAML::Value << renderer.grid.planeZ;
		out << YAML::EndMap;
		out << YAML::EndMap;
	}
} // namespace DefectStudio::ConfigYaml

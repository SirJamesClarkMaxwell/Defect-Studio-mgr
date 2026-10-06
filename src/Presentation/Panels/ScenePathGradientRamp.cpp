#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathGradientRamp.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

#include <imgui.h>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] bool SameStop(const PathGradientStop &first, const PathGradientStop &second)
		{
			return first.position == second.position && first.color == second.color && first.alpha == second.alpha;
		}

		void NormalizeGradient(PathGradient &gradient)
		{
			for (PathGradientStop &stop : gradient.stops)
			{
				if (!std::isfinite(stop.position))
					stop.position = 0.0f;
				stop.position = std::clamp(stop.position, 0.0f, 1.0f);
				if (!std::isfinite(stop.color.x))
					stop.color.x = 1.0f;
				if (!std::isfinite(stop.color.y))
					stop.color.y = 1.0f;
				if (!std::isfinite(stop.color.z))
					stop.color.z = 1.0f;
				if (!std::isfinite(stop.alpha))
					stop.alpha = 1.0f;
			}
			std::stable_sort(gradient.stops.begin(), gradient.stops.end(),
				[](const PathGradientStop &first, const PathGradientStop &second) {
					return first.position < second.position;
				});
		}

		void KeepSelectedStop(PathGradient &gradient, int &selectedStop, const PathGradientStop &selectedValue)
		{
			const auto selected = std::find_if(gradient.stops.begin(), gradient.stops.end(),
				[&selectedValue](const PathGradientStop &stop) { return SameStop(stop, selectedValue); });
			if (selected != gradient.stops.end())
				selectedStop = static_cast<int>(selected - gradient.stops.begin());
			else if (gradient.stops.empty())
				selectedStop = -1;
			else
				selectedStop = std::clamp(selectedStop, 0, static_cast<int>(gradient.stops.size() - 1u));
		}

		[[nodiscard]] bool SameGradient(const PathGradient &first, const PathGradient &second)
		{
			if (first.enabled != second.enabled || first.stops.size() != second.stops.size())
				return false;
			for (std::size_t index = 0; index < first.stops.size(); ++index)
				if (!SameStop(first.stops[index], second.stops[index]))
					return false;
			return true;
		}

		struct MarkerState
		{
			std::vector<PathGradientStop> values;
			std::vector<std::uint64_t> ids;
			std::uint64_t nextId = 1;
			std::uint64_t activeId = 0;
			float dragOffsetX = 0.0f;
		};

		std::unordered_map<std::string, MarkerState> markerStates;

		void SyncMarkerIds(MarkerState &state, const PathGradient &gradient, const int selectedStop)
		{
			std::vector<std::uint64_t> ids(gradient.stops.size(), 0);
			std::vector<bool> used(state.ids.size(), false);
			if (state.activeId != 0 && selectedStop >= 0 &&
				static_cast<std::size_t>(selectedStop) < ids.size())
				ids[static_cast<std::size_t>(selectedStop)] = state.activeId;
			for (std::size_t index = 0; index < gradient.stops.size(); ++index)
			{
				if (ids[index] != 0)
					continue;
				for (std::size_t oldIndex = 0; oldIndex < state.ids.size(); ++oldIndex)
					if (!used[oldIndex] && state.ids[oldIndex] != state.activeId &&
						SameStop(state.values[oldIndex], gradient.stops[index]))
					{
						ids[index] = state.ids[oldIndex];
						used[oldIndex] = true;
						break;
					}
				if (ids[index] == 0)
					ids[index] = state.nextId++;
			}
			state.values = gradient.stops;
			state.ids = std::move(ids);
		}

		[[nodiscard]] std::size_t FindInsertionIndex(const std::vector<PathGradientStop> &stops, const float position)
		{
			const auto next = std::lower_bound(stops.begin(), stops.end(), position,
				[](const PathGradientStop &stop, const float value) { return stop.position < value; });
			return static_cast<std::size_t>(next - stops.begin());
		}

		void DrawGradientBar(ImDrawList &drawList, const ImVec2 &minimum, const ImVec2 &maximum,
			const PathGradient &gradient)
		{
			constexpr int kSegments = 64;
			const float width = maximum.x - minimum.x;
			for (int segment = 0; segment < kSegments; ++segment)
			{
				const float lower = static_cast<float>(segment) / static_cast<float>(kSegments);
				const float upper = static_cast<float>(segment + 1) / static_cast<float>(kSegments);
				const ImVec2 segmentMinimum(minimum.x + width * lower, minimum.y);
				const ImVec2 segmentMaximum(minimum.x + width * upper, maximum.y);
				const glm::vec4 color = SampleGradientAt(gradient, (lower + upper) * 0.5f);
				drawList.AddRectFilled(segmentMinimum, segmentMaximum,
					ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a)));
			}
			drawList.AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_Border));
		}

		struct MarkerGeometry
		{
			float centerX = 0.0f;
			float top = 0.0f;
			float bottom = 0.0f;
			float halfWidth = 0.0f;
		};

		[[nodiscard]] MarkerGeometry GetMarkerGeometry(const ImVec2 &barMinimum, const ImVec2 &barMaximum,
			const float width, const std::vector<PathGradientStop> &stops, const std::size_t index,
			const bool selected, const float itemSpacing)
		{
			constexpr float kMarkerHeight = 14.0f;
			constexpr float kSelectedMarkerBorder = 2.0f;
			const float baseHalfWidth = std::max(4.0f, width * 0.0125f);
			const float markerHalfWidth = baseHalfWidth + kSelectedMarkerBorder;
			const float position = stops[index].position;
			std::size_t first = index;
			while (first > 0 && stops[first - 1].position == position)
				--first;
			std::size_t last = index;
			while (last + 1 < stops.size() && stops[last + 1].position == position)
				++last;

			const std::size_t rank = index - first;
			const std::size_t count = last - first + 1u;
			const float spacing = markerHalfWidth * 2.0f + itemSpacing;
			float offset = 0.0f;
			if (count > 1u)
			{
				const float baseX = barMinimum.x + width * position;
				if (baseX <= barMinimum.x + baseHalfWidth)
					offset = static_cast<float>(rank) * spacing;
				else if (baseX >= barMaximum.x - baseHalfWidth)
					offset = -static_cast<float>(count - 1u - rank) * spacing;
				else
					offset = (static_cast<float>(rank) - static_cast<float>(count - 1u) * 0.5f) * spacing;
			}

			const float centerX = barMinimum.x + width * position + offset;
			const float top = barMaximum.y + 1.0f - (selected ? kSelectedMarkerBorder : 0.0f);
			const float height = kMarkerHeight + (selected ? kSelectedMarkerBorder * 2.0f : 0.0f);
			return MarkerGeometry{centerX, top, top + height,
				selected ? markerHalfWidth : baseHalfWidth};
		}
	}

	glm::vec4 SampleGradientAt(const PathGradient &gradient, const float position)
	{
		if (!gradient.enabled || gradient.stops.empty())
			return glm::vec4(1.0f);
		const auto &stops = gradient.stops;
		if (position <= stops.front().position)
			return glm::vec4(stops.front().color, stops.front().alpha);
		if (position >= stops.back().position)
			return glm::vec4(stops.back().color, stops.back().alpha);
		for (std::size_t index = 1; index < stops.size(); ++index)
			if (position <= stops[index].position)
			{
				const PathGradientStop &a = stops[index - 1];
				const PathGradientStop &b = stops[index];
				const double fraction = (static_cast<double>(position) - a.position) /
					(static_cast<double>(b.position) - a.position);
				return glm::vec4(glm::mix(a.color, b.color, static_cast<float>(fraction)),
					glm::mix(a.alpha, b.alpha, static_cast<float>(fraction)));
			}
		return glm::vec4(1.0f);
	}

	std::size_t InsertGradientStopInWidestGap(PathGradient &gradient)
	{
		NormalizeGradient(gradient);
		std::size_t bestGap = 0;
		float bestWidth = -1.0f;
		for (std::size_t gap = 0; gap <= gradient.stops.size(); ++gap)
		{
			const float lower = gap == 0 ? 0.0f : gradient.stops[gap - 1].position;
			const float upper = gap == gradient.stops.size() ? 1.0f : gradient.stops[gap].position;
			if (upper - lower > bestWidth)
			{
				bestGap = gap;
				bestWidth = upper - lower;
			}
		}

		const float lower = bestGap == 0 ? 0.0f : gradient.stops[bestGap - 1].position;
		const float upper = bestGap == gradient.stops.size() ? 1.0f : gradient.stops[bestGap].position;
		const float position = std::clamp(lower + (upper - lower) * 0.5f, 0.0f, 1.0f);
		const glm::vec4 color = SampleGradientAt(gradient, position);
		PathGradientStop stop{position, glm::vec3(color), color.a};
		const std::size_t index = FindInsertionIndex(gradient.stops, position);
		gradient.stops.insert(gradient.stops.begin() + static_cast<std::ptrdiff_t>(index), stop);
		return index;
	}

	GradientRampResult DrawGradientRamp(const char *id, PathGradient &gradient, int &selectedStop)
	{
		const PathGradient before = gradient;
		const bool hadSelection = selectedStop >= 0 &&
			static_cast<std::size_t>(selectedStop) < gradient.stops.size();
		const PathGradientStop previousSelection = hadSelection
			? gradient.stops[static_cast<std::size_t>(selectedStop)] : PathGradientStop{};
		NormalizeGradient(gradient);
		if (gradient.stops.empty())
		{
			gradient.enabled = false;
			selectedStop = -1;
		}
		else
		{
			if (hadSelection)
				KeepSelectedStop(gradient, selectedStop, previousSelection);
			else
				selectedStop = std::clamp(selectedStop, 0, static_cast<int>(gradient.stops.size() - 1u));
		}

		const std::string stateKey = id == nullptr ? "##gradientRamp" : id;
		MarkerState &markerState = markerStates[stateKey];
		SyncMarkerIds(markerState, gradient, selectedStop);
		GradientRampResult result;
		ImGui::PushID(stateKey.c_str());

		const ImGuiStyle &style = ImGui::GetStyle();
		ImGui::SameLine(0.0f, style.ItemSpacing.x);
		if (ImGui::Button("+"))
		{
			selectedStop = static_cast<int>(InsertGradientStopInWidestGap(gradient));
			markerState.activeId = 0;
		}
		ImGui::SameLine();
		if (ImGui::Button("-") && !gradient.stops.empty())
		{
			gradient.stops.erase(gradient.stops.begin() + selectedStop);
			if (gradient.stops.empty())
			{
				gradient.enabled = false;
				selectedStop = -1;
			}
			else
				selectedStop = std::min(selectedStop, static_cast<int>(gradient.stops.size() - 1u));
			markerState.activeId = 0;
		}
		ImGui::NewLine();
		SyncMarkerIds(markerState, gradient, selectedStop);

		// Inset the bar by a marker's half width (+ selection border) on both sides: markers at 0 and
		// 1 otherwise stick out past the content region, and an auto-sized panel grows to fit them
		// every frame - which widens the bar and the markers again.
		const float available = ImGui::GetContentRegionAvail().x;
		const float inset = std::max(4.0f, available * 0.0125f) + 2.0f;
		const float width = available - 2.0f * inset;
		constexpr float kMinimumWidth = 1.0f;
		if (!std::isfinite(width) || width < kMinimumWidth)
		{
			NormalizeGradient(gradient);
			if (gradient.stops.empty())
			{
				gradient.enabled = false;
				selectedStop = -1;
			}
			else
				selectedStop = std::clamp(selectedStop, 0, static_cast<int>(gradient.stops.size() - 1u));
			SyncMarkerIds(markerState, gradient, selectedStop);
			ImGui::PopID();
			result.changed = !SameGradient(before, gradient);
			return result;
		}
		constexpr float kBarHeight = 24.0f;
		constexpr float kMarkerHeight = 14.0f;
		const ImVec2 barMinimum(ImGui::GetCursorScreenPos().x + inset, ImGui::GetCursorScreenPos().y);
		const ImVec2 barMaximum(barMinimum.x + width, barMinimum.y + kBarHeight);
		ImGui::Dummy(ImVec2(available, kBarHeight + kMarkerHeight + 4.0f + style.ItemSpacing.y));
		const ImVec2 nextItem = ImGui::GetCursorScreenPos();
		DrawGradientBar(*ImGui::GetWindowDrawList(), barMinimum, barMaximum, gradient);

		bool reordered = false;
		const int selectionAtFrameStart = selectedStop;
		for (std::size_t index = 0; index < gradient.stops.size(); ++index)
		{
			const MarkerGeometry geometry = GetMarkerGeometry(barMinimum, barMaximum, width,
				gradient.stops, index, static_cast<int>(index) == selectionAtFrameStart, style.ItemSpacing.x);
			ImGui::SetCursorScreenPos(ImVec2(geometry.centerX - geometry.halfWidth, geometry.top));
			ImGui::PushID(static_cast<int>(markerState.ids[index]));
			ImGui::InvisibleButton("##marker",
				ImVec2(geometry.halfWidth * 2.0f, geometry.bottom - geometry.top));
			if (ImGui::IsItemActivated())
			{
				selectedStop = static_cast<int>(index);
				markerState.activeId = markerState.ids[index];
				markerState.dragOffsetX = ImGui::GetMousePos().x - geometry.centerX;
				result.dragStarted = true;
			}
			if (ImGui::IsItemActive())
			{
				const float position = std::clamp((ImGui::GetMousePos().x - markerState.dragOffsetX - barMinimum.x) /
					width, 0.0f, 1.0f);
				if (position != gradient.stops[index].position)
				{
					gradient.stops[index].position = position;
					std::vector<std::pair<PathGradientStop, std::uint64_t>> markers;
					markers.reserve(gradient.stops.size());
					for (std::size_t marker = 0; marker < gradient.stops.size(); ++marker)
						markers.emplace_back(gradient.stops[marker], markerState.ids[marker]);
					std::stable_sort(markers.begin(), markers.end(),
						[](const auto &first, const auto &second) {
							return first.first.position < second.first.position;
						});
					for (std::size_t marker = 0; marker < markers.size(); ++marker)
					{
						gradient.stops[marker] = markers[marker].first;
						markerState.ids[marker] = markers[marker].second;
						if (markerState.ids[marker] == markerState.activeId)
							selectedStop = static_cast<int>(marker);
					}
					reordered = true;
				}
			}
			if (ImGui::IsItemDeactivated())
			{
				result.dragEnded = true;
				markerState.activeId = 0;
			}
			ImGui::PopID();
			if (reordered)
				break;
		}
		ImGui::SetCursorScreenPos(nextItem);

		ImDrawList &drawList = *ImGui::GetWindowDrawList();
		for (std::size_t index = 0; index < gradient.stops.size(); ++index)
		{
			const bool selected = static_cast<int>(index) == selectedStop;
			const MarkerGeometry geometry = GetMarkerGeometry(barMinimum, barMaximum, width,
				gradient.stops, index, selected, style.ItemSpacing.x);
			const ImVec2 tip(geometry.centerX, geometry.top);
			const ImVec2 left(geometry.centerX - geometry.halfWidth, geometry.bottom);
			const ImVec2 right(geometry.centerX + geometry.halfWidth, geometry.bottom);
			if (selected)
				drawList.AddTriangleFilled(tip, left, right, ImGui::GetColorU32(ImGuiCol_Text));
			const float innerTop = selected ? geometry.top + 2.0f : geometry.top;
			const float innerBottom = selected ? geometry.bottom - 2.0f : geometry.bottom;
			const float innerHalfWidth = selected ? geometry.halfWidth - 2.0f : geometry.halfWidth;
			const ImU32 markerColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
				gradient.stops[index].color.r, gradient.stops[index].color.g,
				gradient.stops[index].color.b, 1.0f));
			drawList.AddTriangleFilled(ImVec2(geometry.centerX, innerTop),
				ImVec2(geometry.centerX - innerHalfWidth, innerBottom),
				ImVec2(geometry.centerX + innerHalfWidth, innerBottom), markerColor);
		}

		if (!gradient.stops.empty())
		{
			selectedStop = std::clamp(selectedStop, 0, static_cast<int>(gradient.stops.size() - 1u));
			PathGradientStop &stop = gradient.stops[static_cast<std::size_t>(selectedStop)];
			ImGui::Text("Selected stop");
			const bool positionChanged = ImGui::DragFloat("Position", &stop.position, 0.01f, 0.0f, 1.0f, "%.3f");
			if (ImGui::IsItemActivated())
				result.dragStarted = true;
			if (positionChanged)
			{
				stop.position = std::clamp(stop.position, 0.0f, 1.0f);
				const PathGradientStop editedStop = stop;
				std::stable_sort(gradient.stops.begin(), gradient.stops.end(),
					[](const PathGradientStop &first, const PathGradientStop &second) {
						return first.position < second.position;
					});
				KeepSelectedStop(gradient, selectedStop, editedStop);
			}
			if (ImGui::IsItemDeactivatedAfterEdit())
				result.dragEnded = true;
			PathGradientStop &selected = gradient.stops[static_cast<std::size_t>(selectedStop)];
			ImGui::ColorEdit3("Color", &selected.color.x);
			if (ImGui::IsItemActivated())
				result.dragStarted = true;
			if (ImGui::IsItemDeactivatedAfterEdit())
				result.dragEnded = true;
			ImGui::SliderFloat("Alpha", &selected.alpha, 0.0f, 1.0f, "%.2f");
			if (ImGui::IsItemActivated())
				result.dragStarted = true;
			if (ImGui::IsItemDeactivatedAfterEdit())
				result.dragEnded = true;
		}

		NormalizeGradient(gradient);
		if (gradient.stops.empty())
		{
			gradient.enabled = false;
			selectedStop = -1;
		}
		else
			selectedStop = std::clamp(selectedStop, 0, static_cast<int>(gradient.stops.size() - 1u));
		SyncMarkerIds(markerState, gradient, selectedStop);
		ImGui::PopID();
		result.changed = !SameGradient(before, gradient);
		return result;
	}
} // namespace DefectStudio

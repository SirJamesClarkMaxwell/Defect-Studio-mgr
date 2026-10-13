#include "Core/dspch.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "App/Events/ApplicationConfigEvents.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Commands/CommandService.hpp"
#include "Core/Commands/SystemCommandEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/PlatformEventBase.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Core/Utils/Input.hpp"
#include "Core/Utils/KeyCodes.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Notifications/NotificationEvents.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Core/Domain/StructureLifecycleEvents.hpp"
#include "Domain/DomainLayer.hpp"
#include "Events/EditorUiEvents.hpp"
#include "Events/ProjectEvents.hpp"
#include "Events/RendererEvents.hpp"
#include "IO/PoscarWriter.hpp"
#include "IO/POTCARWriter.hpp"
#include "IO/ProjectManifestIO.hpp"
#include "IO/EnergeticsIO.hpp"
#include "IO/FnvCorrectionsIO.hpp"
#include "IO/ProjectRootsIO.hpp"
#include "IO/RecentProjectsIO.hpp"
#include "IO/SceneObjectsIO.hpp"
#include "IO/TextFileIO.hpp"
#include "Presentation/ProjectSceneSave.hpp"
#include "Renderer/ProjectSceneWindow.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/Panels/BondSettingsPanel.hpp"
#include "Presentation/Panels/NewStructureWizardPanel.hpp"
#include "Presentation/Panels/CalculatorConsolePanel.hpp"
#include "Presentation/Panels/ElectronicStructurePanel.hpp"
#include "Presentation/Panels/ElectronicStructureSession.hpp"
#include "Presentation/Panels/SceneDensityLoader.hpp"
#include "Presentation/Panels/ExportImagePanel.hpp"
#include "Presentation/Panels/OccupationDiagramPanel.hpp"
#include "Presentation/Panels/DisplacementComparisonPanel.hpp"
#include "Presentation/Panels/GroupTheoryPanel.hpp"
#include "Presentation/Panels/FnvCorrectionPanel.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"
#include "Presentation/Panels/DefectEnergeticsSession.hpp"
#include "Presentation/Panels/CalculationSummarySession.hpp"
#include "Presentation/Panels/ElementCatalogPanel.hpp"
#include "Presentation/Panels/ObjectPropertiesPanel.hpp"
#include "Presentation/Panels/RendererPanel.hpp"
#include "Presentation/Panels/SceneOutlinerPanel.hpp"
#include "Presentation/Panels/SettingsPanel.hpp"
#include "Presentation/Panels/MaterialsCollectionPanel.hpp"
#include "Presentation/Panels/StructureCreationTabsPanel.hpp"
#include "Presentation/Panels/StructureHubPanel.hpp"
#include "Presentation/Panels/TerminalPanel.hpp"
#include "Presentation/Panels/TextEditorPanel.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererStartupBootstrap.hpp"

namespace DefectStudio
{
	[[nodiscard]] Path ProjectWindowsStatePath()
	{
		return Path::FromResolved(
			FileSystem::CurrentPath() / "install" / "users" / "default" / "config" / "project_windows.txt");
	}

	[[nodiscard]] Path PanelVisibilityStatePath()
	{
		return Path::FromResolved(
			FileSystem::CurrentPath() / "install" / "users" / "default" / "config" / "panel_visibility.txt");
	}

	[[nodiscard]] std::vector<std::string> SplitOn(const std::string &text, char delimiter)
	{
		std::vector<std::string> parts;
		std::size_t start = 0;
		while (true)
		{
			const std::size_t pos = text.find(delimiter, start);
			parts.push_back(text.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
			if (pos == std::string::npos)
				break;
			start = pos + 1;
		}
		return parts;
	}

	// Minimal hand-rolled block format ("key=value" lines, "---" between windows) - not real YAML,
	// deliberately: this file is internal-only (nothing else reads it), and pulling yaml-cpp
	// ceremony into EditorLayer.cpp for one save/load pair isn't worth it. See
	// EditorLayer::saveProjectWindowState/loadAndQueueProjectWindowRestores for how it's used.
	[[nodiscard]] std::string SerializeWindowRecord(const PersistedWindowRecord &record)
	{
		std::ostringstream stream;
		stream << "source=" << record.sourcePath.String() << '\n';
		stream << "display_name=" << record.displayName << '\n';
		stream << "view=" << SerializeViewSnapshot(record.view) << '\n';
		stream << "show=" << record.showAtoms << ',' << record.showBonds << ',' << record.showCellBox << ','
			   << record.showGrid << '\n';
		// Its own line rather than a fifth field in "show=": that one is read back only when it splits
		// into exactly 4 parts, so widening it would silently reset every toggle in every file written
		// before today.
		stream << "show_periodic_bonds=" << record.showPeriodicBonds << '\n';
		stream << "orbital_up=" << record.orbitalUpEnabled << ',' << record.orbitalUpPositiveColor.x << ','
			   << record.orbitalUpPositiveColor.y << ',' << record.orbitalUpPositiveColor.z << ','
			   << record.orbitalUpNegativeColor.x << ',' << record.orbitalUpNegativeColor.y << ','
			   << record.orbitalUpNegativeColor.z << ',' << record.orbitalUpAlpha << '\n';
		stream << "orbital_down=" << record.orbitalDownEnabled << ',' << record.orbitalDownPositiveColor.x << ','
			   << record.orbitalDownPositiveColor.y << ',' << record.orbitalDownPositiveColor.z << ','
			   << record.orbitalDownNegativeColor.x << ',' << record.orbitalDownNegativeColor.y << ','
			   << record.orbitalDownNegativeColor.z << ',' << record.orbitalDownAlpha << '\n';
		stream << "has_es=" << record.hasElectronicStructureState << '\n';
		if (record.hasElectronicStructureState)
		{
			stream << "es=" << record.esBandStart << ',' << record.esBandEnd << ',' << record.esGapWindowMargin
				   << ',' << record.esLocalizationThreshold << ',' << record.esSplitSpinChannels << ','
				   << record.esRelativeToVbm << ',' << record.esSelectedBand << ',' << record.esIsoValue << '\n';
			stream << "es_iso_overrides=";
			bool first = true;
			for (const auto &[spinChannel, band, value] : record.esIsoOverrides)
			{
				if (!first)
					stream << ';';
				first = false;
				stream << spinChannel << ':' << band << ':' << value;
			}
			stream << '\n';
		}
		stream << "---\n";
		return stream.str();
	}

	[[nodiscard]] std::vector<PersistedWindowRecord> ParseProjectWindowsState(const std::string &text)
	{
		std::vector<PersistedWindowRecord> records;
		std::unordered_map<std::string, std::string> fields;
		std::istringstream stream(text);
		std::string line;

		const auto flush = [&]() {
			if (!fields.contains("source"))
			{
				fields.clear();
				return;
			}
			PersistedWindowRecord record;
			record.sourcePath = Path(fields["source"]);
			record.displayName = fields.contains("display_name") ? fields["display_name"] : std::string{};
			if (fields.contains("view"))
			{
				if (std::optional<RendererViewSnapshot> snapshot = DeserializeViewSnapshot(fields["view"]))
					record.view = *snapshot;
			}
			if (fields.contains("show"))
			{
				const std::vector<std::string> parts = SplitOn(fields["show"], ',');
				if (parts.size() == 4)
				{
					record.showAtoms = parts[0] == "1";
					record.showBonds = parts[1] == "1";
					record.showCellBox = parts[2] == "1";
					record.showGrid = parts[3] == "1";
				}
			}
			if (fields.contains("show_periodic_bonds"))
				record.showPeriodicBonds = fields["show_periodic_bonds"] == "1";
			const auto parseOrbital =
				[&](const char *key, bool &enabled, glm::vec3 &pos, glm::vec3 &neg, float &alpha) {
					if (!fields.contains(key))
						return;
					const std::vector<std::string> parts = SplitOn(fields[key], ',');
					if (parts.size() != 8)
						return;
					try
					{
						enabled = parts[0] == "1";
						pos = glm::vec3(std::stof(parts[1]), std::stof(parts[2]), std::stof(parts[3]));
						neg = glm::vec3(std::stof(parts[4]), std::stof(parts[5]), std::stof(parts[6]));
						alpha = std::stof(parts[7]);
					}
					catch (const std::exception &)
					{
					}
				};
			parseOrbital(
				"orbital_up", record.orbitalUpEnabled, record.orbitalUpPositiveColor, record.orbitalUpNegativeColor,
				record.orbitalUpAlpha);
			parseOrbital(
				"orbital_down", record.orbitalDownEnabled, record.orbitalDownPositiveColor,
				record.orbitalDownNegativeColor, record.orbitalDownAlpha);

			record.hasElectronicStructureState = fields.contains("has_es") && fields["has_es"] == "1";
			if (record.hasElectronicStructureState && fields.contains("es"))
			{
				const std::vector<std::string> parts = SplitOn(fields["es"], ',');
				if (parts.size() == 8)
				{
					try
					{
						record.esBandStart = std::stoi(parts[0]);
						record.esBandEnd = std::stoi(parts[1]);
						record.esGapWindowMargin = std::stoi(parts[2]);
						record.esLocalizationThreshold = std::stof(parts[3]);
						record.esSplitSpinChannels = parts[4] == "1";
						record.esRelativeToVbm = parts[5] == "1";
						record.esSelectedBand = std::stoi(parts[6]);
						record.esIsoValue = std::stof(parts[7]);
					}
					catch (const std::exception &)
					{
					}
				}
			}
			if (fields.contains("es_iso_overrides") && !fields["es_iso_overrides"].empty())
			{
				for (const std::string &token : SplitOn(fields["es_iso_overrides"], ';'))
				{
					const std::vector<std::string> parts = SplitOn(token, ':');
					if (parts.size() != 3)
						continue;
					try
					{
						record.esIsoOverrides.emplace_back(
							std::stoi(parts[0]), std::stoi(parts[1]), std::stof(parts[2]));
					}
					catch (const std::exception &)
					{
					}
				}
			}

			records.push_back(std::move(record));
			fields.clear();
		};

		while (std::getline(stream, line))
		{
			if (line == "---")
			{
				flush();
				continue;
			}
			const std::size_t eq = line.find('=');
			if (eq == std::string::npos)
				continue;
			fields[line.substr(0, eq)] = line.substr(eq + 1);
		}
		flush();

		return records;
	}

	void EditorLayer::saveProjectWindowState()
	{
		auto rendererLayer = m_RendererLayer.lock();
		if (rendererLayer == nullptr)
			return;

		std::ostringstream stream;
		for (const RendererWindowState &windowState : rendererLayer->GetWindows())
		{
			if (windowState.structure.sourcePath.Empty())
				continue;

			PersistedWindowRecord record;
			record.sourcePath = windowState.structure.sourcePath;
			record.displayName = windowState.title;
			if (std::optional<RendererViewSnapshot> snapshot =
					rendererLayer->CaptureWindowViewSnapshot(windowState.windowId))
				record.view = *snapshot;
			record.showAtoms = windowState.showAtoms;
			record.showBonds = windowState.showBonds;
			record.showPeriodicBonds = windowState.showPeriodicBonds;
			record.showCellBox = windowState.showCellBox;
			record.showGrid = windowState.showGrid;
			record.orbitalUpEnabled = windowState.orbitalChannelUp.enabled;
			record.orbitalUpPositiveColor = windowState.orbitalChannelUp.positiveLobeColor;
			record.orbitalUpNegativeColor = windowState.orbitalChannelUp.negativeLobeColor;
			record.orbitalUpAlpha = windowState.orbitalChannelUp.lobeAlpha;
			record.orbitalDownEnabled = windowState.orbitalChannelDown.enabled;
			record.orbitalDownPositiveColor = windowState.orbitalChannelDown.positiveLobeColor;
			record.orbitalDownNegativeColor = windowState.orbitalChannelDown.negativeLobeColor;
			record.orbitalDownAlpha = windowState.orbitalChannelDown.lobeAlpha;

			if (m_ElectronicStructureSession != nullptr)
			{
				if (const ElectronicStructureSession::WindowState *esState =
						m_ElectronicStructureSession->FindWindowState(windowState.windowId))
				{
					record.hasElectronicStructureState = true;
					record.esBandStart = esState->bandStart;
					record.esBandEnd = esState->bandEnd;
					record.esGapWindowMargin = esState->gapWindowMargin;
					record.esLocalizationThreshold = esState->localizationThreshold;
					record.esSplitSpinChannels = esState->splitSpinChannels;
					record.esRelativeToVbm = esState->relativeToVbm;
					record.esSelectedBand = esState->selectedBand;
					record.esIsoValue = esState->isoValue;
					for (const auto &[key, value] : esState->isoValueByKey)
					{
						const int spinChannel = static_cast<int>(key >> 32);
						const int band = static_cast<int>(key & 0xffffffffu);
						record.esIsoOverrides.emplace_back(spinChannel, band, value);
					}
				}
			}

			stream << SerializeWindowRecord(record);
		}

		std::string error;
		if (!TextFileIO::Save(ProjectWindowsStatePath(), stream.str(), error))
			DS_LOG_WARN("EditorLayer: failed to persist project window state: {}", error);
	}

	void EditorLayer::loadAndQueueProjectWindowRestores()
	{
		std::string text;
		std::string error;
		if (!TextFileIO::Load(ProjectWindowsStatePath(), text, error))
			return;

		for (PersistedWindowRecord &record : ParseProjectWindowsState(text))
		{
			if (record.sourcePath.Empty() || !FileSystem::Exists(record.sourcePath.Native()))
				continue;

			const std::string predictedId = ComputeDeterministicRendererWindowId(record.sourcePath);
			m_PendingWindowRestores[predictedId] = record;

			if (m_EventBus != nullptr)
			{
				RendererEvents::Windows::OpenStructureRequested request;
				request.filePath = record.sourcePath;
				request.displayName = record.displayName;
				m_EventBus->Queue(request);
			}
		}
	}

	void EditorLayer::pollPendingWindowRestores()
	{
		auto rendererLayer = m_RendererLayer.lock();
		if (rendererLayer == nullptr)
			return;

		for (RendererWindowState &windowState : rendererLayer->GetWindows())
		{
			applySceneObjectsToWindow(windowState);
			if (m_PendingWindowRestores.empty())
				continue;
			const auto it = m_PendingWindowRestores.find(windowState.windowId);
			if (it == m_PendingWindowRestores.end())
				continue;

			const PersistedWindowRecord &record = it->second;
			rendererLayer->ApplyWindowViewSnapshot(windowState.windowId, record.view);
			windowState.showAtoms = record.showAtoms;
			windowState.showBonds = record.showBonds;
			windowState.showPeriodicBonds = record.showPeriodicBonds;
			windowState.showCellBox = record.showCellBox;
			windowState.showGrid = record.showGrid;
			windowState.orbitalChannelUp.enabled = record.orbitalUpEnabled;
			windowState.orbitalChannelUp.positiveLobeColor = record.orbitalUpPositiveColor;
			windowState.orbitalChannelUp.negativeLobeColor = record.orbitalUpNegativeColor;
			windowState.orbitalChannelUp.lobeAlpha = record.orbitalUpAlpha;
			windowState.orbitalChannelDown.enabled = record.orbitalDownEnabled;
			windowState.orbitalChannelDown.positiveLobeColor = record.orbitalDownPositiveColor;
			windowState.orbitalChannelDown.negativeLobeColor = record.orbitalDownNegativeColor;
			windowState.orbitalChannelDown.lobeAlpha = record.orbitalDownAlpha;

			if (record.hasElectronicStructureState && m_ElectronicStructureSession != nullptr)
			{
				ElectronicStructureSession::WindowState &esState = m_ElectronicStructureSession->Update(windowState);
				esState.bandStart = record.esBandStart;
				esState.bandEnd = record.esBandEnd;
				esState.gapWindowMargin = record.esGapWindowMargin;
				esState.localizationThreshold = record.esLocalizationThreshold;
				esState.splitSpinChannels = record.esSplitSpinChannels;
				esState.relativeToVbm = record.esRelativeToVbm;
				esState.selectedBand = record.esSelectedBand;
				esState.isoValue = record.esIsoValue;
				for (const auto &[spinChannel, band, value] : record.esIsoOverrides)
					esState.isoValueByKey[ElectronicStructureSession::PackGridKey(spinChannel, band)] = value;

				m_ElectronicStructureSession->DispatchOutputLoad(esState);

				if (windowState.orbitalChannelUp.enabled)
					m_ElectronicStructureSession->EnsureChannelRendered(esState, windowState, 0, 0);
				if (windowState.orbitalChannelDown.enabled)
					m_ElectronicStructureSession->EnsureChannelRendered(esState, windowState, 1, 1);
			}

			m_PendingWindowRestores.erase(it);
		}
	}

	void EditorLayer::savePanelVisibilityState()
	{
		std::ostringstream stream;
		for (const Entry &entry : m_Panels.Entries())
		{
			if (entry.panel == nullptr)
				continue;
			stream << entry.panel->GetTitle() << '=' << (entry.panel->IsVisible() ? '1' : '0') << '\n';
		}

		std::string error;
		if (!TextFileIO::Save(PanelVisibilityStatePath(), stream.str(), error))
			DS_LOG_WARN("EditorLayer: failed to persist panel visibility state: {}", error);
	}

	void EditorLayer::applyPanelVisibilityState()
	{
		std::string text;
		std::string error;
		if (!TextFileIO::Load(PanelVisibilityStatePath(), text, error))
			return;

		std::unordered_map<std::string, bool> visibilityByTitle;
		std::istringstream stream(text);
		std::string line;
		while (std::getline(stream, line))
		{
			const std::size_t separator = line.find('=');
			if (separator == std::string::npos)
				continue;
			visibilityByTitle[line.substr(0, separator)] = line.substr(separator + 1) == "1";
		}

		for (Entry &entry : m_Panels.Entries())
		{
			if (entry.panel == nullptr)
				continue;
			const auto it = visibilityByTitle.find(entry.panel->GetTitle());
			if (it != visibilityByTitle.end())
				entry.panel->SetVisible(it->second);
		}
	}

} // namespace DefectStudio

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
	template <typename EventType>
	SubscriptionHandle subscribeEditorLayer(
		EventBus &bus,
		EditorLayer &layer,
		void (EditorLayer::*method)(const EventType &),
		EventPriority priority = EventPriority::Normal)
	{
		return bus.Subscribe<EventType>(std::bind_front(method, &layer), priority);
	}

	void EditorLayer::bindConfigEvents()
	{
		if (m_EventBus == nullptr)
		{
			DS_LOG_WARN("EditorLayer config event binding skipped: EventBus unavailable");
			return;
		}

		using namespace AppEvents::Config;
		AddSubscription(subscribeEditorLayer<Applied>(*m_EventBus, *this, &EditorLayer::onConfigApplied, EventPriority::High));
		AddSubscription(subscribeEditorLayer<CoreEvents::OpenCommandPaletteRequested>(
			*m_EventBus,
			*this,
			&EditorLayer::onOpenCommandPaletteRequested,
			EventPriority::High));
		// project.save (Ctrl+S, CoreLayer::registerSystemCommands) previously only reached
		// ApplicationEventController::onProjectSaveRequested, which just logs - Ctrl+S looked like it
		// worked but silently did nothing, while File>Zapisz bypassed the whole command/event path and
		// called persistCurrentRoots() directly. Routing the same event here makes Ctrl+S do the real
		// save too, instead of deleting the shortcut or leaving it dead.
		AddSubscription(subscribeEditorLayer<CoreEvents::ProjectSaveRequested>(
			*m_EventBus, *this, &EditorLayer::onProjectSaveRequested));
		AddSubscription(subscribeEditorLayer<CoreEvents::ProjectSaveAsRequested>(
			*m_EventBus, *this, &EditorLayer::onProjectSaveAsRequested));
		AddSubscription(subscribeEditorLayer<CoreEvents::StructureFileSaveRequested>(
			*m_EventBus, *this, &EditorLayer::onStructureFileSaveRequested));
		DS_LOG_INFO("EditorLayer config event handlers bound");
	}

	void EditorLayer::bindProjectRootEvents()
	{
		if (m_EventBus == nullptr)
		{
			DS_LOG_WARN("EditorLayer project-root event binding skipped: EventBus unavailable");
			return;
		}

		AddSubscription(subscribeEditorLayer<ProjectEvents::RootAddRequested>(
			*m_EventBus, *this, &EditorLayer::onRootAddRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::RootRemoveRequested>(
			*m_EventBus, *this, &EditorLayer::onRootRemoveRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::RootPathChangedRequested>(
			*m_EventBus, *this, &EditorLayer::onRootPathChangedRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::BulkDirectoryChangeRequested>(
			*m_EventBus, *this, &EditorLayer::onBulkDirectoryChangeRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<DomainEvents::ProjectTreeSelectionChanged>(
			*m_EventBus, *this, &EditorLayer::onProjectTreeSelectionChanged, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<DomainEvents::ProjectStructureAdded>(
			*m_EventBus, *this, &EditorLayer::onProjectStructureAdded, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::SceneObjectsModified>(
			*m_EventBus, *this, &EditorLayer::onSceneObjectsModified, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<RendererEvents::Viewport::WavecarDropped>(
			*m_EventBus, *this, &EditorLayer::onWavecarDropped, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::TextFileOpenRequested>(
			*m_EventBus, *this, &EditorLayer::onTextFileOpenRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::CalculationSummaryOpenRequested>(
			*m_EventBus, *this, &EditorLayer::onCalculationSummaryOpenRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::EnergeticsFoldersAddRequested>(
			*m_EventBus, *this, &EditorLayer::onEnergeticsFoldersAddRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::IrrepLabelOverridesChanged>(
			*m_EventBus, *this, &EditorLayer::onIrrepLabelOverridesChanged, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::DisplacementComparisonStateChanged>(
			*m_EventBus, *this, &EditorLayer::onDisplacementComparisonStateChanged, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::PhononDirectoryRequested>(
			*m_EventBus, *this, &EditorLayer::onPhononDirectoryRequested, EventPriority::Normal));
		AddSubscription(m_EventBus->Subscribe<RendererEvents::Windows::CloseRequested>(
			[this](const RendererEvents::Windows::CloseRequested &) { stopPhononAnimations(); }, EventPriority::Highest));
		AddSubscription(subscribeEditorLayer<ProjectEvents::DisplacementComparisonFileRequested>(
			*m_EventBus, *this, &EditorLayer::onDisplacementComparisonFileRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<RendererEvents::Viewport::DisplacementComparisonPanelRequested>(
			*m_EventBus, *this, &EditorLayer::onDisplacementComparisonPanelRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::DisplacementComparisonFilePickRequested>(
			*m_EventBus, *this, &EditorLayer::onDisplacementComparisonFilePickRequested, EventPriority::Normal));
		AddSubscription(subscribeEditorLayer<ProjectEvents::DisplacementComparisonFilePicked>(
			*m_EventBus, *this, &EditorLayer::onDisplacementComparisonFilePicked, EventPriority::Normal));
	}


	void EditorLayer::onProjectTreeSelectionChanged(const DomainEvents::ProjectTreeSelectionChanged &event)
	{
		if (auto panel = findPanel(m_StructureHubPanelId).lock())
		{
			if (auto *hub = dynamic_cast<StructureHubPanel *>(panel.get()))
			{
				hub->SetTargetDirectory(Path::FromResolved(event.resolvedTargetDirectory));
			}
		}

		// The wizard gets the selected FILE (one of its four load routes). Forwarded from here
		// rather than subscribed a second time: this handler already runs for exactly this event.
		if (auto panel = findPanel(m_NewStructureWizardPanelId).lock())
		{
			if (auto *wizard = dynamic_cast<NewStructureWizardPanel *>(panel.get()))
			{
				wizard->SetProjectTreeSelection(
					event.kind == DomainEvents::ProjectTreeSelectionChanged::Kind::File
						? Path::FromResolved(event.selectedPath)
						: Path{});
			}
		}
	}

	void EditorLayer::onIrrepLabelOverridesChanged(const ProjectEvents::IrrepLabelOverridesChanged &event)
	{
		if (m_ElectronicStructureSession != nullptr)
			m_ElectronicStructureSession->SetIrrepLabelOverrides(event.overrides);

		if (m_ActiveProject.has_value())
		{
			m_ActiveProject->irrepLabelOverrides = event.overrides;
			std::string error;
			if (!ProjectManifestIO::Save(m_ActiveProjectDirectory, *m_ActiveProject, error))
				DS_LOG_WARN("EditorLayer: failed to save manifest.yaml after irrep label change: {}", error);
		}
	}

	void EditorLayer::onDisplacementComparisonStateChanged(const ProjectEvents::DisplacementComparisonStateChanged &event)
	{
		if (!m_ActiveProject.has_value())
			return;

		m_ActiveProject->displacementComparisonPath = event.comparisonFilePath;
		m_ActiveProject->displacementThresholdAngstrom = event.thresholdAngstrom;
		std::string error;
		if (!ProjectManifestIO::Save(m_ActiveProjectDirectory, *m_ActiveProject, error))
			DS_LOG_WARN("EditorLayer: failed to save manifest.yaml after displacement comparison change: {}", error);
	}

	void EditorLayer::onDisplacementComparisonFileRequested(const ProjectEvents::DisplacementComparisonFileRequested &event)
	{
		if (auto panel = findPanel(m_DisplacementComparisonPanelId).lock())
		{
			if (auto *displacementPanel = dynamic_cast<DisplacementComparisonPanel *>(panel.get()))
				displacementPanel->SetComparisonFile(event.filePath);
		}
	}

	void EditorLayer::onDisplacementComparisonPanelRequested(const RendererEvents::Viewport::DisplacementComparisonPanelRequested &event)
	{
		if (auto panel = findPanel(m_DisplacementComparisonPanelId).lock())
		{
			if (auto *displacementPanel = dynamic_cast<DisplacementComparisonPanel *>(panel.get()))
				displacementPanel->OpenForWindow(event.windowId);
		}
	}

	void EditorLayer::onDisplacementComparisonFilePickRequested(const ProjectEvents::DisplacementComparisonFilePickRequested &)
	{
		if (auto panel = findPanel(m_ProjectTreePanelId).lock())
		{
			if (auto *treePanel = dynamic_cast<ProjectTreePanel *>(panel.get()))
				treePanel->RequestFilePick();
		}
	}

	void EditorLayer::onDisplacementComparisonFilePicked(const ProjectEvents::DisplacementComparisonFilePicked &event)
	{
		if (auto panel = findPanel(m_DisplacementComparisonPanelId).lock())
		{
			if (auto *displacementPanel = dynamic_cast<DisplacementComparisonPanel *>(panel.get()))
				displacementPanel->SetComparisonFile(event.filePath);
		}
	}

	void EditorLayer::onTextFileOpenRequested(const ProjectEvents::TextFileOpenRequested &event)
	{
		if (auto panel = findPanel(m_TextEditorPanelId).lock())
		{
			if (auto *textEditor = dynamic_cast<TextEditorPanel *>(panel.get()))
				textEditor->OpenFile(event.path);
		}
	}

	void EditorLayer::onWavecarDropped(const RendererEvents::Viewport::WavecarDropped &event)
	{
		if (m_ElectronicStructureSession == nullptr)
			return;

		auto rendererLayer = m_RendererLayer.lock();
		if (rendererLayer == nullptr)
			return;

		for (RendererWindowState &windowState : rendererLayer->GetWindows())
		{
			if (windowState.windowId != event.windowId)
				continue;

			ElectronicStructureSession::WindowState &state = m_ElectronicStructureSession->Update(windowState);
			state.calculationDirectory = event.wavecarPath.parent_path();
			m_ElectronicStructureSession->DispatchOutputLoad(state);
			DS_LOG_INFO("WAVECAR dropped on window '{}': calculationDirectory set to '{}'",
				event.windowId, state.calculationDirectory.String());
			return;
		}
	}

} // namespace DefectStudio

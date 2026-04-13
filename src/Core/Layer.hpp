#pragma once

#include "Core/dspch.hpp"
#include "Core/Platform/Events/PlatformEvent.hpp"

namespace DefectStudio
{
	class Layer
	{
	public:
		explicit Layer(std::string name = "Layer");
		virtual ~Layer();

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate(float) {}
		virtual void OnEvent(Event &) {}
		virtual void OnImGuiRender() {}

		[[nodiscard]] const std::string &GetName() const;

	private:
		std::string m_Name;
	};
} // namespace DefectStudio

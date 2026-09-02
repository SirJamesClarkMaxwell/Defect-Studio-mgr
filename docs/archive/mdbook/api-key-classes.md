# API Key Classes

This page describes primary classes and ownership boundaries in the current baseline.

## Application

Responsibilities:

- Owns runtime startup and shutdown flow.
- Creates and runs the main GUI loop.
- Applies command-line runtime options.

Current entry points:

- Application::Run in src/App/Application.cpp
- ParseArguments in src/main.cpp

## ApplicationOptions

Purpose:

- Transport runtime options from CLI to application startup.

Current fields:

- logLevel
- logToFile
- logFilePath
- projectPath
- safeMode
- resetLayout

## Logger

Responsibilities:

- Centralized logger initialization and shutdown.
- Runtime logger access through DefectStudio::Logger::Get().
- Macro-based convenience logging via DS_LOG_*.

Current source:

- src/Core/Logger.hpp
- src/Core/Logger.cpp

## DefectStudioTests

Purpose:

- Dedicated executable for unit and smoke tests.
- Built independently from the application target.

Current baseline:

- tests/Core/LoggerTests.cpp

## Near-Term API Expansion

Planned key APIs from TODO:

- Ref and WeakRef helpers and ownership conventions.
- LayerStack and event interfaces.
- ProjectWorkspace and registry access APIs.
- Scientific Runtime bridge contracts and DTO shapes.

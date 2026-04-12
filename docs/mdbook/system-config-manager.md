# Config Manager

## What It Is

`ConfigManager` handles application configuration files and format abstraction.

## How It Works

The system loads and stores configuration documents and supports YAML/JSON inputs.

Project direction:

- YAML as the primary format,
- JSON as compatibility/migration support.

## How To Use It

Use `ConfigManager` as the single entry point for config IO. Avoid scattered ad-hoc config reads across UI and runtime layers.

Practical rule: `Application` loads configuration during startup, and downstream systems consume already prepared values.

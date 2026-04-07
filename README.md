# Defect Studio

<p align="center">
  <img src="assets/banner.png" alt="Defect Studio banner" width="650" />
</p>

## Project Overview

Defect Studio is a desktop-oriented scientific UI application  focused on repeatable defect-analysis workflows.
It combines a C++ build pipeline with Python-based automation to keep environment setup and project operations consistent.
The repository is designed around script-first onboarding so both technical and non-technical contributors can run standard tasks safely.


## Setup Guide (Windows and Linux)
### Windows
```ps1
git clone https://github.com/sjcmdev/Defect-Studio.git  // 1. clone repository
cd Defect-Studio
.\scripts\Windows\ConfigurePreferences.bat 				// 2. Configure local preferences
.\scripts\Windows\Setup.bat 							// 3. Run Setup configuration
.\scripts\Windows\GenerateProjects.bat --verbose		// 4. Generate project
.\scripts\Windows\Build.bat --dry-run --verbose			// 5. Build and compile
.\scripts\Windows\BuildDocs.bat --verbose				// 6. Generate local documentation
```
### Linux
```bash
git clone https://github.com/sjcmdev/Defect-Studio.git // 1. clone repository
cd Defect-Studio
chmod +x ./scirpts/Linux/*.sh						   // 2. Add executable privlages to scripts
./scripts/Linux/ConfigurePreferences.sh				   // 3. Configure local preferences
./scripts/Linux/Setup.sh							   // 4. Run Setup configuration
./scripts/Linux/GenerateProjects.sh --verbose		   // 5. Generate project	
./scripts/Linux/Build.sh --dry-run --verbose		   // 6. Build and compile	
./scripts/Linux/BuildDocs.sh --verbose				   // 7. Generate local documentation	
```
Expected outcome:
- Project generation finishes without errors.
- Build dry-run prints valid commands and paths.
- Documentation build completes and prints a local preview URL.

## For Developers

- Use the setup wrappers as the default entrypoints instead of calling internal Python scripts directly.
- Keep tooling changes idempotent and cross-platform, and prefer extending existing scripts under scripts/python and scripts/common.
- Store project-specific defaults with ConfigurePreferences so command behavior stays stable between runs.
- Before opening a PR, run generation, a build dry-run, and docs build to catch integration regressions early.
- Follow architecture and process docs under docs/work for project conventions and implementation checkpoints.
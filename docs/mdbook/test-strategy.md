# Test Strategy

This page defines practical testing scope for the current platform setup stage.

## Current Layers of Validation

- Build matrix across compilers and configurations.
- Test matrix for Release and Dist test target execution.
- Runtime smoke run through application wrappers.

## Canonical Commands

Build matrix:

- scripts/Windows/TestBuildMatrix.bat --continue-on-error
- bash ./scripts/Linux/TestBuildMatrix.sh --continue-on-error

Release and Dist test matrix:

- scripts/Windows/TestReleaseDistTests.bat --continue-on-error
- bash ./scripts/Linux/TestReleaseDistTests.sh --continue-on-error

## Immediate Goals from TODO

- Expand unit tests around parser and model contracts.
- Add migration regression checks.
- Keep CI checks deterministic and script-first.

## Baseline Rule

No branch should merge without passing the configured matrix relevant to its scope.

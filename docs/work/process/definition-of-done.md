# Definition of Done

A task is considered done only when the implemented change is correct, reviewable, and aligned with the project architecture.

## Core rule

“Code compiles” is not enough.  
“Feature seems to work” is not enough.  
A task is done only when the result is safe to keep in the codebase.

## Required conditions

### 1. Build and basic verification
- Debug build passes.
- Release build passes, if the task affects production code.
- The change was verified in the smallest realistic workflow it affects.

### 2. Architecture and boundaries
- The change respects the agreed architecture.
- No forbidden boundary was crossed.
- Domain ownership was not moved accidentally into UI, ECS, renderer, IO, or Python runtime code.
- No hidden persistence logic was introduced into parser/export code.
- No direct live-state mutation was added to background jobs.

### 3. Code quality
- The code follows `cpp-guidelines.md`.
- The code follows `ai-guidelines.md` when AI-generated.
- Ownership and lifetime are understandable.
- The change is small enough to review calmly.
- No speculative framework or abstraction was introduced without a real need.

### 4. Error handling and diagnostics
- Failure paths are handled according to the project error model.
- Important failures are not hidden behind `bool`.
- Logging is useful and not used as a substitute for proper error handling.
- Assertions, if added, have no side effects.

### 5. Tests
- Tests were added or updated when the change introduces risk at a boundary.
- Parsers, serialization, runtime boundaries, and structured workflows get tests when justified.
- If no test was added, there is a conscious reason.

### 6. Documentation
- Relevant documentation was updated if code meaning changed.
- TODO progress was updated if the task status changed.
- Temporary compromises are visible and scoped.

### 7. Reviewer confidence
- The change can be explained clearly.
- The next likely maintainer can understand what was changed and why.
- The result is something worth keeping, not just something that “works for now”.

## Additional rule for AI-generated code

AI-generated code is not done only because it compiled or because it looks plausible.  
It is done only after human review confirms correctness, clarity, architectural fit, and maintainability.

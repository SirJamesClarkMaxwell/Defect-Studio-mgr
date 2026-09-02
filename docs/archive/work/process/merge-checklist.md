# Merge Checklist

Before merging, confirm the following.

## Scope and clarity
- The change solves one clear problem.
- The change is small enough to review calmly.
- The commit history is understandable.

## Architecture
- No forbidden boundary was crossed.
- Domain did not absorb UI, renderer, ECS, IO, or Python-runtime concerns.
- Storage and IO remain separate.
- Background jobs do not directly mutate live project state.
- No accidental production dependency on DebugLayer was introduced.

## Code quality
- The code follows `cpp-guidelines.md`.
- The AI-generated parts follow `ai-guidelines.md`.
- Ownership and lifetime are understandable.
- Shared ownership was not introduced without a clear reason.
- Lambdas were not used to hide important logic.

## Error handling
- Failure is not hidden behind `bool` when the reason matters.
- Logs are useful but not a substitute for handling the error.
- Exceptions were not introduced as the default application error model.
- Assertions do not have side effects.

## Tests and verification
- The change was verified in the workflow it affects.
- Tests were added or updated when justified.
- Risky boundaries were not changed without verification.

## Documentation
- Relevant documentation was updated if needed.
- TODO progress was updated if task state changed.
- Temporary compromises are visible and scoped.

## Final question
- Is this change something worth keeping in the codebase six months from now?

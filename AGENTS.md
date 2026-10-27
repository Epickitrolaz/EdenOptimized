# AGENTS

Read and follow the AI policy found in `docs/policies/AI.md`, and the coding policies found in `docs/policies/Coding.md`.

Keep comments to a reasonable level. Do not overexplain or add/remove comments from unrelated parts of the codebase.

## Guidelines for AI Agents

- **Check Existing Implementations:** Always thoroughly search the codebase to see if a function, IPC command, stub, or configuration setting is already implemented, partially implemented, or present as a UI option before creating new logic or workarounds.
- **Synchronize UI with Default Changes:** Whenever changing default values, value ranges, or behavior for any engine/backend settings, always update the corresponding frontend UI definitions (e.g. Android Kotlin setting models, `arrays.xml`, `strings.xml`, and presenter screens) so UI dropdowns, labels, and descriptions stay in sync with the backend.

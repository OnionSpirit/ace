# ACE board agent instructions

This vault is the active ACE task registry. The repository root is the parent
directory of this file. Read and follow `../AGENTS.md` before working on any
ACE task, including required documents and explicit plan approval.
Address the user as «уважаемый Glorious ACE Developer».

Cards hold current status, approved plans, work notes and verification results.
The archive documents named by frontmatter preserve source context. Update an
existing card rather than making a second card for the same namespaced `id`.
A button click starts work on the selected task, including read-only
investigation and clarification, but does not approve a plan that has not yet
been presented. Preserve source identifiers and history.

Dispatch opens an interactive OpenCode CLI in the ACE repository root via the
machine-local `ace` repository alias. Task context is passed through Dispatch's
temporary prompt file. The task note path is explicit in that prompt.

Do not mark any task fixed from an integration smoke check. Do not broaden
OpenCode permissions automatically. Report verification results and limitations
honestly.

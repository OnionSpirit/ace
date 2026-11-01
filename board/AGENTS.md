# ACE board agent instructions

This vault is the active ACE task registry. The repository root is the parent
directory of this file. Read and follow `../AGENTS.md` before working on any
ACE task, including required documents and explicit plan approval.
Address the user as «уважаемый Glorious ACE Developer».

Cards hold current status, approved plans, work notes and verification results.
The archive documents named by frontmatter preserve source context. Update an
existing card rather than making a second card for the same namespaced `id`.
A button click starts research for a queued task or exactly one approved phase.
The researcher investigates, links related tasks, dependents and blockers,
proposes new cards when appropriate, and presents phases, models and effort.
Only after explicit plan approval does the researcher record the agreed plan
and move the card to `Разобрано`. Preserve source identifiers and history.
Each phase records results and completed checklist items before stopping; on
failure it remains incomplete and the card is blocked. User decides whether
to proceed despite unresolved blockers. The project instructions take priority.

Dispatch invokes the machine-local ObsidianBoard phase launcher using the `ace`
repository alias. The researcher is interactive; each later click runs exactly
one approved phase with its designated model and effort. The terminal title
identifies the task and phase. Personal agent settings are not rewritten.

Do not mark any task fixed from an integration smoke check. Do not broaden
Codex permissions automatically. Report verification results and limitations
honestly.

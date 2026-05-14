# Implementation Sync Checklist

Version: 2026-05-14-14-23

Use this checklist before and after implementation updates to keep code, specs,
and PRD synchronized.

## Pre-implementation

- Confirm canonical PRD in docs/pm-prd/PRD.md is current.
- Confirm canonical specs in docs/dev-specs/*.md are current.
- Confirm PRD-to-spec mapping in docs/dev-specs/overview.md reflects target scope.

## During implementation

- Update only modules covered by approved specs.
- Keep Kconfig symbol behavior aligned with module specs.
- Preserve zbus channel contracts in src/modules/messages.h unless spec update is approved.

## Post-implementation (required)

- Update src/main.c to include:
  - `#define SPECS_VERSION "<latest docs/dev-specs/overview.md changelog timestamp>"`
- Print SPECS_VERSION in startup logs.
- Verify SPECS_VERSION equals latest specs changelog timestamp.
- If SPECS_VERSION changes, append changelog row in affected specs and PRD when behavior changed.

## Release-readiness checks

- Create a new docs/qa-test/QA-YYYY-MM-DD-HH-MM.md snapshot.
- Ensure README still matches project structure and workflow commands.
- Validate CI uses a single-line NCS_VERSION output from west.yml extraction.

---
name: query-urma-docs
description: Query and search URMA documentation. Use when asked to find URMA API references, look up functions, or search documentation for URMA concepts.
---

Query the URMA Chinese documentation at `doc/ch/urma/`. These are markdown docs covering the URMA API guide, user guide, and quickstart guide.

All paths below are relative to this skill directory (`.claude/skills/query-urma-docs/`).

## Prerequisite tools

```bash
grep    # built-in
rg --help 2>/dev/null || true  # ripgrep, fall back to grep if missing
```

## How to query

**Find a function/API by name:**
```bash
grep -r "urma_create_jfc" .
```

**Find a concept (e.g. Jetty, Segment, JFR):**
```bash
grep -r "Jetty" .
```

**List all doc files:**
```bash
ls *.md
```

**Read the full doc to answer complex questions:**
```bash
cat "URMA API Guide.ch.md"
```

## Content summary

| File | What it covers |
|---|---|
| `URMA API Guide.ch.md` | All URMA API functions: init, device, context, JFC/JFCE/JFAE/JFS/JFR/Jetty, Segment, token, EID |
| `URMA User Guide.ch.md` | URMA architecture, concepts (UB, UBVA, Jetty, Segment), DFX, tools, 生态兼容 |
| `URMA QuickStart Guide.ch.md` | Build from source, install RPM, kernel module, quick verification |

## Quick answers

**What's a Jetty?** → `grep -m5 "Jetty" "URMA User Guide.ch.md"`

**How to create a JFC?** → `grep -A10 "urma_create_jfc" "URMA API Guide.ch.md"`

**How to compile URMA?** → see `URMA QuickStart Guide.ch.md` §1

## Notes

- All docs are in Chinese with Chinese filenames
- No code or binary to run — this is a reference docs collection
- No build step needed
- Docs are embedded inside the skill directory for portability
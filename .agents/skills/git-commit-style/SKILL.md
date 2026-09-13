---
name: git-commit-style
description: Define consistent Git commit message conventions for the project. Use when creating commits, reviewing commit messages, or generating AI-assisted commits.
---

# Git Commit Style

## Objective
Keep Git history readable, searchable, and consistent across all contributors and AI assistants.

## Global Constraints
Apply these constraints while using this skill.

1. Follow the established commit conventions; do not introduce alternative styles.
2. Keep commit messages factual and concise; avoid vague or overly generic descriptions.
3. Respect the existing project commit history as the reference for tone and granularity.

## Commit Message Format

Full format:
```
<type>(<scope>): <short description> — <detailed body>
```

Simplified format for trivial changes:
```
<type>(<scope>): <short description>
```

## Type

Use lowercase English types, compatible with Conventional Commits.

| Type | Meaning | Example |
|------|---------|---------|
| `feat` | New feature | `feat: ScriptCardPainter dynamic layout engine` |
| `refactor` | Code refactoring (no bug fix, no feature) | `refactor: ElaMultiComboBox Fluent UI` |
| `fix` | Bug fix | `fix(ElaAppBar): fix WM_GETMINMAXINFO DPI multi-screen scaling error` |
| `docs` | Documentation changes | `docs: project docs + Memory Bank + Agent Skills` |
| `chore` | Miscellaneous (build, tooling, deps) | `chore(ElaWidgetTools): remove redundant Q_SIGNAL and virtual modifiers` |
| `revert` | Revert a previous commit | Generated automatically by `git revert` |

## Scope (optional)

- Component or module name, placed in parentheses after the type, with the first letter capitalized.
- Examples: `(ElaAppBar)`, `(ScriptPage)`, `(painter)`, `(delegate)`, `(ElaWidgetTools)`
- Omit the scope when the change spans multiple components.

## Language and Separator

- Both the subject line and body must use **Simplified Chinese**.
- Separate the short description from the detailed body with an **em dash (—)** surrounded by a single space on each side.

## Style Rules

- ❌ Do not use emojis (✨ ♻ 📝 etc.)
- ✅ Use English type names (e.g., `refactor`), never Chinese equivalents
- ✅ A single space must follow the colon `:` that separates the type/scope and the description
- ✅ Use bullet points (`-`) for each change point in the body

## Examples

```
refactor(ElaPopupBase): refactor Slide animation engine — direction-aware offset + bitflag fix + opacity guard

- Fix bug where bitwise OR was used instead of AND, causing all animation branches to execute unconditionally
- Fix Fade animation operating on windowOpacity while being obscured by QGraphicsOpacityEffect(opacity=0)
- Refactor Slide animation to be direction-aware
- Add AnimePosition enum to allow external override of direction
- Add ElaPopupBasePrivate private implementation header (PIMPL separation)

feat(delegate): implement Delegate Layout Cache to avoid repeated layout calculations in paint()

- CardLayoutCacheManager singleton with quad-tuple cache key
- sizeHint/paint/editorEvent all read from cache
- Layout offset fix; animation does not pollute cache

fix(ElaAppBar): fix WM_GETMINMAXINFO DPI multi-screen scaling error

- WM_GETMINMAXINFO callback coordinate calculation was wrong in multi-DPI environments
- Fall back to primaryScreen logicalDotsPerInch
```

## Commit Granularity

- Each commit must represent **one independent logical change**.
- A single commit should touch **no more than 31 files** and stay within **500–800 lines** of change (split if larger).
- **Buildable**: every commit must compile and pass basic sanity checks on its own.

## Commit Message Checklist

1. Type is correct and matches the nature of the change.
2. Scope is included when the change is limited to a single component; omitted when cross-cutting.
3. Short description is in Chinese, concise, and starts with a verb or noun describing the change.
4. Em dash separator is present when a body exists.
5. Body uses `-` bullet points for each logical change detail.
6. No emojis or non-standard decorations.
7. Commit size respects file and line limits; large changes are split logically.
8. The commit builds independently.

---

*This specification is derived from the project’s recent commit history and applies to all contributors and AI coding assistants.*
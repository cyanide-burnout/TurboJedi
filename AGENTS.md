# Notes for coding agents

## Releases

Every minor release has a theme of its own (0.12 is selections, 0.11 is distribution and remote journals), a patch
release finishes the theme of the one before it. A release is made only when the maintainer asks for it.

1. Add a section for the version to the top of [CHANGELOG.md](CHANGELOG.md), above the previous one:

   ```
   ## 0.13 — 2026-11-20

   Theme in a few words.

   - A change as a user sees it, one item per feature or fix
   ```

   A patch release has no line of the theme. The heading has to be exactly `## <version> — <date>`: GitHub Actions
   finds the section by the second word of the heading. Items may wrap, their lines are joined for the release.
2. Commit it. The section has to be in the commit the tag points to: the notes of the release come from the tag, a
   section added after it is not taken.
3. Tag that commit with an annotated tag and push both:

   ```
   git tag -a v0.13 -m "TurboJedi 0.13"
   git push origin master v0.13
   ```

   GitHub Actions builds the packages and publishes the release with the section in front of the link to the full
   changelog. A release without a section gets the link alone; add the section afterwards and run
   `gh release edit v0.13 --notes-file <file>` with the section and that link.

When a release is deleted (its tag stays), its section stays in CHANGELOG.md and goes into the notes of the next
published release under `### <version> — <date>`, with the link of the full changelog starting at the release
published before it.

## Commits

One line in the style of the history: `Area: what it does now, as a user sees it`, without a trailer of a co-author.

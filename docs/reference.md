# Reference: master 2e43ae8

The reference source remains available with `git show 2e43ae8:<path>`.
Before replacement, build-win/LWSSettingsTests.exe passed 33 non-viewer cases
(290 assertions); build-release/LWSSettingsDemo.exe --smoke exited successfully.

Reference appearance: dark background #171c25, foreground #e5eaf2,
accent #66b7ff, changed values #f2c66d; settings tree above a description area,
search above the tree and reset/apply/cancel/save actions below it.

Interaction contract recorded from the reference source and existing checks:
- Nested groups collapse; searching reveals matching descendants and breadcrumbs.
- Combo overlays own hit testing and retain their owner's description.
- Text, rocker and color edits preview values and confirm once per interaction.
- Escape restores the edit start; invalid text remains a draft and blocks confirmation.
- Numeric holds accelerate; leaving the rocker pauses repetition.
- RGB, alpha and HSL share a color; achromatic colors retain hue.
- File text is editable and F4 opens the native file dialog.
- Working values are isolated until Apply in staged mode; Cancel restores applied.
- Notifications identify ID, old/new typed values, phase, origin and value domain.

This baseline was initially recorded from source and automated checks. The live
Windows comparison and interaction follow-up are recorded in `verification.md`.

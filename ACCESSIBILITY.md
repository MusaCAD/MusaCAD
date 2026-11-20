# Accessibility

Musa CAD should be usable by people with different access needs. Improving keyboard
access, readable controls, and compatibility with assistive technology is part of
improving the application. This statement describes the current capabilities and
limitations; it is not a claim that every part of the application is accessible.

## Platforms and available controls

Musa CAD is a Qt 6 desktop application for Linux and Windows, with macOS builds
available for testing. Available packages and platform requirements are listed in
the [installation guide](https://wiki.musacad.org/get-started/).

- Commands and numeric coordinates can be entered from the keyboard. See the
  [command reference](https://wiki.musacad.org/reference/) and
  [keyboard shortcuts](https://wiki.musacad.org/reference/keyboard-shortcuts/).
- The command line provides text prompts and command history. Dynamic Input can be
  toggled with `F12` when prompts next to the cursor are difficult to use.
- Many dialogs use standard Qt controls, but this alone does not establish
  compatibility with any particular screen reader.
- Recent GUI checks cover selected workflows at 100% and 200% display scaling;
  they are not a full accessibility review.

## Known limitations

- The drawing canvas is rendered with OpenGL. Drawing geometry and spatial
  relationships do not currently have a complete screen-reader representation.
- Some custom controls, tabs, toolbars, selection operations, and mouse-driven
  workflows may not be fully keyboard accessible. Completing a drawing using only
  the keyboard has not been fully tested.
- Complete workflows have not been validated with NVDA, JAWS, VoiceOver, or Orca.
  There is no verified list of supported screen-reader and operating-system combinations.
- High-contrast modes, very large text, and all combinations of display scaling have
  not been comprehensively checked. Dense ribbon panels and floating controls may
  present barriers.
- Tagged PDF output and the reading order of plotted drawings have not been verified
  for assistive technology.

Priority areas are clear focus and accessible names, reliable keyboard navigation,
readable prompts at larger scales, and alternatives to information conveyed only
through color or position. Changes should be checked with keyboard navigation and,
where useful, a screen reader or other assistive technology as well as automated tests.

## Report a barrier

Use the [accessibility issue form](https://github.com/MusaCAD/MusaCAD/issues/new?template=accessibility.yml)
or email [pranay@weberq.in](mailto:pranay@weberq.in) if GitHub is difficult to use
or you prefer a private report.

Describe the task, where progress was blocked, and what would help. Include the
application version/build and relevant operating system, assistive technology, or
display settings when possible. Screenshots and videos are optional. A diagnosis or
medical history is not needed.

Reports are reviewed with attention to barriers that prevent completing a task.
How soon a problem can be fixed depends on the work needed and the time available.

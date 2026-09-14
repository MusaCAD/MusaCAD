<!-- Draft of the Flathub submission pull request body. It is YOURS to finish: Flathub's
     checklist asks the submitter to affirm things only a person can (including that no
     AI tool or agent generated or automated the pull request), so read every line, put
     it in your own words, replace [ ] with [X] only where it is true, and remove this
     comment. The two [TODO] lines need you. -->

### Please confirm your submission meets all the criteria

- [ ] Please describe the application briefly. Musa CAD is a 2D CAD application for engineering drafting: lines, arcs, polylines, splines, hatches, dimensions with tolerances and GD&T, tables, blocks with attributes, external references, raster images, layouts with viewports, and vector PDF plotting; drawings save in a native format and exchange as DXF. LGPL-3.0-or-later, https://musacad.org/, https://github.com/MusaCAD/MusaCAD.
- [ ] Please attach a video showcasing the application on Linux using the Flatpak. [TODO: record a short screen capture of the Flatpak build (`flatpak run org.musacad.MusaCAD`) and attach it here]
- [ ] The Flatpak ID follows all the rules listed in the [Application ID requirements][appid]. (`org.musacad.MusaCAD` is the reverse of the project's domain, musacad.org.)
- [ ] I have read and followed all the [Submission requirements][reqs] and the [Submission guide][reqs2] and I agree to them.
  - [ ] The application has a meaningful development history, evidence of real-world use, and a clear commitment to ongoing maintenance, as required by the [development history requirements][history]. (Public repository since June 2026 with four tagged releases, Linux and Windows builds, an issue-driven roadmap and a release process in `docs/RELEASING.md`.)
  - [ ] I have disclosed any AI-generated material included in the application or its Flathub packaging, as required by the [Generative AI policy][ai]. **Affected parts and approximate extent:** [TODO: state this in your own words -- a substantial part of the application's source code and this Flatpak packaging (manifest and metainfo) was written with an AI coding assistant under the maintainer's direction and review; the maintainer is responsible for all of it]
  - [ ] I have not used AI tools or agents to generate or automate this submission pull request or its review interactions.
- [ ] I am an author/developer/upstream contributor to the project.

**Requested exception -- `finish-args-home-filesystem-access`:** the manifest keeps `--filesystem=home` because a drawing refers to files beside it by relative path: external references (XREF), attached raster images, and the image files a DXF export writes next to the DXF. A single portal-picked file does not give access to those, so the folder around the drawing is needed. Built-in DXF read/write works without anything else; DWG conversion is a separate opt-in that runs a host-installed converter through the Flatpak portal, which the user grants explicitly.

[appid]: https://docs.flathub.org/docs/for-app-authors/requirements#application-id
[reqs]: https://docs.flathub.org/docs/for-app-authors/requirements
[reqs2]: https://docs.flathub.org/docs/for-app-authors/submission
[history]: https://docs.flathub.org/docs/for-app-authors/requirements#insufficient-development-history
[ai]: https://docs.flathub.org/docs/for-app-authors/requirements#generative-ai-policy

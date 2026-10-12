# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

## [0.4.0] - 2026-10-12

### Added
- Added cis- and trans-1,2-dichloroethene, boat and chair cyclohexane, hydrogen peroxide, and oxalic acid to the molecule library
- Added export of any matrix as an Excel workbook, CSV, or tab-separated plain text with full stored precision
- Added coefficient matrices for all available orbital sets, with basis functions as rows and molecular orbitals as columns

### Changed
- Sorted the molecule library alphabetically by name, ignoring capitalization

## [0.3.0] - 2026-09-30

Tested with PyQInt 1.4.3, PyDFT 1.0.0, Python 3.12 and uv 0.12.20.

### Added
- Added Kohn-Sham density functional theory single points through [PyDFT](https://ifilot.github.io/pydft/) (Theory → Density functional theory), with the SVWN5 (LDA) and PBE (GGA) functionals, a choice of the angular integration grid, live SCF convergence and optional Foster-Boys localization; the generated `job.py` uses only the public PyDFT API
- Added validation of the limitations of PyDFT 1.0 with plain-language explanations: only neutral closed-shell molecules with elements H to Ar, no geometry optimizations, and no control over the maximum number of iterations, DIIS or orthogonalization
- Added the Hartree (J) and exchange-correlation (Vxc) matrices, the correlation energy and the PyDFT version to the results of DFT calculations, and a warning when the SCF did not converge
- Added PyDFT to the managed Python environment and its status to the environment dialog and the status bar; environments without PyDFT remain usable for Hartree-Fock
- Added a link to the PyDFT manual to the Help menu
- Added a message log (Help → Message log) that collects the diagnostic messages of the program, with copy, save and clear; the `--verbose` command-line option also prints them to the terminal, and `--show log` opens the log

### Changed
- Renamed the program from PyQInt-GUI to Praxis, since it now covers PyDFT as well as PyQInt: the executable is `praxis`, the installers are `Praxis-Windows-Setup.exe` and `Praxis-macOS-arm64.dmg`, the job helper module is `praxis_export.py`, and the CMake options and test variables start with `PRAXIS_`; settings, job folders and the managed Python environment now live under `IMC/Praxis`, so the environment is installed again on first launch
- Changed debug messages (rendering, model loading) to go to the message log instead of the terminal; warnings and errors are still printed as well
- Changed "Use tested PyQInt" and "Update to latest PyQInt" in the environment dialog to "Use tested versions" and "Update to latest", which install or upgrade PyQInt and PyDFT together
- Changed Foster-Boys localization of an existing result (`localize.py`) to also accept DFT results

## [0.2.0] - 2026-09-30

Tested with PyQInt 1.4.3, Python 3.12 and uv 0.12.20.

### Added
- Added an orbital gallery (Analysis → Orbital gallery, Ctrl+G) that shows all orbitals, the occupied orbitals, the frontier orbitals or a custom range in a grid; all orbitals rotate together, can be shown from preset directions (including face-on and edge-on views derived from the principal axes of the molecule, or the orientation of the main viewer) and can be exported as one PNG image per orbital
- Added an orbital bonding analysis (Analysis → Orbital bonding analysis, or the Charges tab) with the orbital-resolved Hamilton (MOHP), overlap (MOOP) and bond-index (MOBI) populations of a pair of atoms, computed in the GUI from the stored matrices (identical to `pyqint.PopulationAnalysis`, and extended to unrestricted calculations); atoms are chosen by clicking them in 3D and are highlighted
- Added Foster-Boys localization of an existing restricted result (Localize button in the Orbitals tab, or Analysis → Localize orbitals) through a generated `localize.py` script that rebuilds the Hartree-Fock result from `result.json` instead of repeating the calculation
- Added face-on and edge-on camera alignments to View → Camera
- Added a link to the GitHub repository to the Help menu and the About dialog
- Added screenshots to the README
- Added the `--show gallery|bonding` command-line option to open (and, with `--screenshot`, capture) the orbital gallery or the bonding analysis
- Added unit tests for the bonding analysis (against PyQInt reference values), localization scripts, camera orientations and highlight meshes, plus an opt-in end-to-end localization test

### Changed
- Changed the menu, toolbar and button icons to the Bluecurve icon theme (the icons of the stereoscopic projection modes are unchanged)
- Changed mouse rotation to be twice as sensitive: dragging across the full height of the viewer now rotates the molecule by a full turn instead of half a turn
- Changed chemical formulas to use subscripts (molecule library, calculation panel and summary) and the molecule library to show the selected row in white text

### Fixed
- Fixed the name of lithium hydride in the molecule library

## [0.1.0]

Initial version, tested with PyQInt 1.4.3, Python 3.12 and uv 0.12.20.

### Added
- Added a Qt 6 desktop application for setting up, running and analysing PyQInt Hartree-Fock calculations without writing Python
- Added a managed Python environment: on first launch the bundled uv downloads a standalone Python interpreter and installs the tested PyQInt version into a private per-user folder, leaving other Python installations untouched
- Added an environment dialog (Python → Manage environment) to install, repair, reset, return to the tested PyQInt version, upgrade to the latest PyQInt release, or use a custom Python interpreter
- Added restricted and unrestricted Hartree-Fock single-point calculations, geometry optimizations and Foster-Boys localization, with the STO-3G, STO-6G, 3-21G, 6-31G and aug-cc-pVXZ basis sets
- Added validation of charge, multiplicity and method before a calculation is started, with automatic selection of a compatible spin state and plain-language explanations
- Added a stand-alone job script (`job.py`) per calculation that uses only the public PyQInt API, together with `result.json`, `output.log` and the molecule in a dedicated job folder
- Added live SCF convergence and geometry-optimization energy plots, and cancellation of running calculations
- Added a molecule library with the molecules shipped with PyQInt, and loading and saving of `.xyz` files
- Added 3D molecular-orbital isosurfaces evaluated in C++ from the basis set and coefficients, with an automatic isovalue enclosing a chosen fraction of the density, a manual isovalue, adjustable grid resolution and opacity
- Added results tabs for a summary (energies, energy decomposition, frontier orbitals, timings), orbitals (including the largest basis-function contributions), an MO energy-level diagram, Mulliken and Löwdin charges, all intermediate matrices, and the geometry-optimization trajectory
- Added stereoscopic (anaglyph and interlaced) rendering and camera controls derived from Managlyph
- Added File → Save image, and the `--screenshot` and `--results-tab` command-line options for producing documentation images
- Added Windows (NSIS) and Apple Silicon macOS (disk image) packaging with the pinned, checksum-verified uv executable
- Added automatic download of uv next to the executable for builds from source (CMake option `PYQINT_GUI_FETCH_UV`)
- Added a GitHub Actions workflow that builds and tests on Linux, Windows and macOS, runs an environment-installation integration test, and attaches the installers to tagged releases
- Added unit tests for molecule parsing, job validation, script generation, result parsing, orbital evaluation against PyQInt reference values and marching cubes, plus opt-in end-to-end tests

### Fixed
Fixes relative to the renderer adopted from Managlyph:
- Fixed heap corruption on exit caused by the OpenGL context invoking `cleanup()` on an already destroyed viewer widget
- Fixed a crash when the OpenGL context is recreated (for example after re-docking), by releasing all GPU resources on context loss and re-uploading models afterwards
- Fixed stale shader programs being kept after re-initialization because existing programs were never replaced
- Fixed a memory leak on every atom-color lookup
- Fixed an uninitialized variable and data race when determining the largest atom distance in a structure

# Praxis

Graphical user interface for two educational electronic-structure programs:
[PyQInt](https://ifilot.github.io/pyqint/) (Hartree-Fock) and
[PyDFT](https://ifilot.github.io/pydft/) (Kohn-Sham density functional
theory). Praxis lets students set up and run calculations without writing
any Python, and visualizes molecules and molecular orbitals in 3D.

Praxis was called PyQInt-GUI up to version 0.2.

![Main window: the HOMO of benzene](docs/img/main.png)

| Orbital gallery | Orbital bonding analysis |
|-----------------|--------------------------|
| ![Frontier orbitals of benzene, all seen from the same direction](docs/img/gallery.png) | ![MOHP analysis of the C=C bond in ethylene](docs/img/bonding.png) |
| All orbitals side by side, rotated together; one PNG per orbital on export. | Which orbitals bond or antibond two atoms (here the C=C bond of ethylene)? |

## Features

* Molecules from a built-in library or from `.xyz` files
* Restricted and unrestricted Hartree-Fock single points (STO-3G ... aug-cc-pVQZ)
* Kohn-Sham DFT single points with PyDFT, using the SVWN5 (LDA) or PBE (GGA)
  functional (see [PyQInt and PyDFT](#pyqint-and-pydft) for the limitations)
* Geometry optimization (Hartree-Fock) with an interactive trajectory viewer
* Foster-Boys localization of the occupied orbitals, either as part of the
  calculation or afterwards for an existing result (without repeating the
  Hartree-Fock calculation)
* Molecular-orbital isosurfaces (isovalue chosen automatically to enclose a
  given fraction of the density, or set manually)
* Orbital gallery: all (or a range of) orbitals in a grid that rotates as one,
  preset orientations (including face-on and edge-on views of planar
  molecules) and export of every orbital as a separate PNG image
* Orbital bonding analysis for any pair of atoms: orbital-resolved Hamilton
  (MOHP), overlap (MOOP) and bond-index (MOBI) populations, with the atoms
  highlighted in 3D
* MO energy-level diagram, Mulliken and Löwdin charges, and all intermediate
  matrices (S, T, V, H, X, F, P and, for DFT, J and Vxc) with basis-function
  labels
* Live SCF convergence plot
* Stereoscopic rendering (anaglyph and interlaced), shared with
  [Managlyph](https://github.com/ifilot/managlyph)

## PyQInt and PyDFT

Choose the program with **Theory** in the calculation panel. PyDFT 1.0 builds
on PyQInt (basis sets, integrals, molecules) but supports fewer kinds of
calculations; the GUI only offers what it supports and explains why a setting
is not available:

| | Hartree-Fock (PyQInt) | DFT (PyDFT) |
|---|---|---|
| Single point | RHF and UHF | closed shell (restricted Kohn-Sham) |
| Charged molecules | yes | no: PyDFT counts electrons from the nuclear charges |
| Open-shell molecules | UHF | no |
| Geometry optimization | RHF | no |
| Elements | all in the basis set | H to Ar (integration grids) |
| Functionals | – | SVWN5 (LDA), PBE (GGA) |
| SCF settings | max. iterations, tolerance, DIIS, orthogonalization | tolerance and angular grid; always DIIS, canonical orthogonalization, at most 100 iterations |
| Foster-Boys localization, bonding analysis | yes (restricted) | yes |

PyDFT does not report whether the SCF converged; the GUI flags a DFT result as
not converged when the last energy change exceeds the tolerance.

## How it works

The installer contains only the program and a copy of
[uv](https://github.com/astral-sh/uv). On first launch Praxis uses uv to
download a standalone Python interpreter and install the tested PyQInt and
PyDFT versions in a private folder:

| Platform | Location                                           |
|----------|----------------------------------------------------|
| Windows  | `%LOCALAPPDATA%\IMC\Praxis\python`                 |
| macOS    | `~/Library/Application Support/IMC/Praxis/python`  |
| Linux    | `~/.local/share/IMC/Praxis/python`                 |

Any Python installation already on the computer is left untouched. The
environment can be updated, reset or replaced by your own interpreter via
**Python → Manage environment**. An environment installed by an earlier
version (PyQInt only) keeps working for Hartree-Fock; click **Use tested
versions** to add PyDFT.

Every calculation runs in its own folder (**File → Show job folders**)
containing:

* `job.py`: a stand-alone script that uses only the public PyQInt (or PyDFT)
  API; run it with `python job.py` to reproduce the calculation outside of
  the GUI
* `praxis_export.py`: helper that writes the results and reports progress
* `result.json`: all results; open it again via **File → Open result**
* `output.log`: the complete output of the calculation
* `localize.py` and `localize.log`: only when the orbitals were localized
  afterwards (**Analysis → Localize orbitals**); the script rebuilds the
  Hartree-Fock or DFT result from `result.json` and adds the Foster-Boys
  orbitals to it

Molecular orbitals are evaluated on a grid by the GUI itself (in C++) from the
basis set and coefficients stored in `result.json`, so changing the isovalue
or grid resolution does not require running Python again. The same holds for
the bonding analysis, which uses the stored Fock, overlap and density matrices
and gives the same values as `pyqint.PopulationAnalysis`.

In the **Matrices** tab, select a matrix and click **Export…** to save it as an
Excel workbook (`.xlsx`), CSV (`.csv`), or tab-separated plain text (`.txt`).
Exports contain the numeric matrix without headers, using the full stored
precision rather than the rounded values displayed in the table.
The selector also includes coefficient matrices for each orbital set (canonical,
Foster–Boys, and separate α/β sets when present), with basis functions as rows
and molecular orbitals as columns.

## Compilation

### Ubuntu / Debian

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools libqt6opengl6-dev libglm-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

For a development build, either place a `uv` executable next to `praxis`
(`scripts/fetch-uv.sh x86_64-unknown-linux-gnu build`), have `uv` on your
`PATH`, or point the program to a Python interpreter in which PyQInt (and,
for DFT, PyDFT) is installed (**Python → Manage environment → Advanced**).

### Windows

Run `./package-windows.sh` in an MSYS2 MinGW64 shell (see
`.github/workflows/ci.yml` for the required packages). This builds the
program, runs the tests, bundles Qt and uv, and creates
`Praxis-Windows-Setup.exe`.

### macOS (Apple Silicon)

```bash
brew install cmake ninja qt glm
bash package-macos.sh
```

### Tests

The unit tests cover molecule parsing, job validation, script generation
(Hartree-Fock and DFT), result parsing,
orbital evaluation and the bonding analysis (both checked against PyQInt
reference values), camera orientations and marching cubes. Three additional
tests are opt-in:

```bash
# run generated job and localization scripts with an existing PyQInt installation
# (script_end_to_end_dft is skipped when PyDFT is not installed)
PRAXIS_TEST_PYTHON=/path/to/python ./build/test/praxis_test script_end_to_end localization_end_to_end script_end_to_end_dft

# install the managed environment with uv and run an HF and a DFT job (downloads ~300 MB)
PRAXIS_TEST_UV=/path/to/uv ./build/test/praxis_test environment_install_and_run
```

## Versions

The PyQInt and PyDFT versions installed into the managed environment and the
Python version are set in `CMakeLists.txt` (`PYQINT_PINNED_VERSION`,
`PYDFT_PINNED_VERSION`, `PYTHON_MANAGED_VERSION`); the bundled uv version and its checksums are set in
`scripts/fetch-uv.sh`.

### Screenshots

The screenshots above are made with the program itself, e.g.

```bash
./build/praxis benzene.json --results-tab orbitals --screenshot docs/img/main.png
./build/praxis benzene.json --show gallery --screenshot docs/img/gallery.png
./build/praxis ethylene.json --show bonding --screenshot docs/img/bonding.png
```

where the `.json` files are results of RHF/STO-3G calculations.

## License

GNU General Public License v3. The 3D renderer is derived from
[Managlyph](https://github.com/ifilot/managlyph). The menu and toolbar icons
are a subset of the Bluecurve icon theme (GPL, see
`assets/icons/bluecurve/README.md`).

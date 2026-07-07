# scripts

Convenience wrappers around the build and workflow steps.

- `build.sh [preset]` configures and builds a CMake preset (default: release).
- `test.sh` builds and runs the full test matrix: release (GCC), ASan + UBSan, and a
  Clang release pass if clang++ is present.
- `demo.sh` runs the end-to-end CLI workflow (synth, validate, run, replay, report) in
  a temporary directory and prints the run id and event log hash.
- `build_python.sh` builds the pybind11 module and copies it into `python/btos/` so
  `import btos` works.
- `run_app.sh` builds the Python module if needed and launches the Streamlit app.

All scripts are shellcheck-clean and resolve paths relative to the repository root, so
they can be run from anywhere.

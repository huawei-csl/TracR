####### PyTraCR: Python bindings built with pybind11

py = import('python').find_installation('python3', pure: false)

# Locate pybind11: try pkg-config/cmake first, then fall back to a
# pip-installed pybind11 (python3 -m pybind11 --includes)
pybind11_dep = dependency('pybind11', required: false)
if not pybind11_dep.found()
    pybind11_includes = run_command(py, ['-m', 'pybind11', '--includes'], check: false)
    if pybind11_includes.returncode() != 0
        error('pybind11 not found. Install it with e.g. "pip install pybind11" or "apt install pybind11-dev".')
    endif
    pybind11_dep = declare_dependency(compile_args: pybind11_includes.stdout().strip().split())
endif

pytracr_module = py.extension_module('tracr',
    'pytracr.cpp',
    install: true,
    dependencies : [InstrumentationBuildDep, pybind11_dep, py.dependency(embed: false)],
    cpp_args: ['-DENABLE_TRACR'],
)

# Directory containing the built tracr Python module (for PYTHONPATH in tests)
pytracr_build_dir = meson.current_build_dir()

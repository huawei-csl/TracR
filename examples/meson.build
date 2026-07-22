testSuite = ['examples']

cleanup_script = find_program('bash_scripts/cleanup_script.sh', native: true, required: true)
multiproc_script = find_program('bash_scripts/multiproc_run.sh', native: true, required: true)

# Number of concurrent instances used to simulate a multiproc (MPI-like) run
multiproc_num_procs = '4'

test_types = ['simple', 'pthread', 'performance_test', 'flow', 'flow_alltoall']

cpp_args_list = [
    ['-DENABLE_TRACR', '-DENABLE_DEBUG'], # Enable TraCR
    []                                    # No instrumentation
]

foreach args : cpp_args_list
    instrumentation_enabled = args.length() > 0 ? true : false

    if instrumentation_enabled
        flag_name = 'tracr_enabled'
    else
        flag_name = 'tracr_disabled'
    endif
    

    foreach test_type : test_types
        test_name = test_type + '_' + flag_name
        exe = executable(test_name, 'tracr/' + test_type + '.cpp', dependencies: InstrumentationBuildDep, cpp_args : args)

        output_dir = exe.path() + '.p'

        if instrumentation_enabled
            test('cleanup_script_' + test_name, cleanup_script, 
                    workdir: output_dir,
                    suite : testSuite,
                    is_parallel : false,
                    priority : 2)

            foreach format : ['paraver', 'perfetto', 'dump']
                test('tracr_process_' + format + '_' + test_name, tracr_process,
                    workdir: meson.current_build_dir() / '../postprocessing',
                    args : [ output_dir / 'tracr', format],
                    suite : testSuite,
                    is_parallel : false,
                    priority : 0)
            endforeach

            # Simulated multiproc run (no MPI needed): the same example is
            # launched multiproc_num_procs times concurrently into a shared
            # tracr/ folder, then postprocessed in every format. Negative
            # priorities sequence this after the single-proc tests above.
            test('cleanup_script_multiproc_' + test_name, cleanup_script,
                    workdir: output_dir,
                    suite : testSuite,
                    is_parallel : false,
                    priority : -1)

            test('multiproc_' + test_name, multiproc_script,
                    args : [multiproc_num_procs, exe],
                    workdir: output_dir,
                    suite : testSuite,
                    is_parallel : false,
                    priority : -2)

            foreach format : ['paraver', 'perfetto', 'dump']
                test('tracr_process_multiproc_' + format + '_' + test_name, tracr_process,
                    workdir: meson.current_build_dir() / '../postprocessing',
                    args : [ output_dir / 'tracr', format],
                    suite : testSuite,
                    is_parallel : false,
                    priority : -3)
            endforeach
        endif

        test(test_name, exe,
            workdir: output_dir,
            suite : testSuite,
            is_parallel : false,
            priority : 1)
    endforeach
endforeach

####### PyTraCR examples (need the tracr Python module built)

if get_option('buildPyTraCR')
    pytracr_examples = ['simple', 'threads', 'flow']

    foreach ex : pytracr_examples
        test_name = 'pytracr_' + ex
        output_dir = meson.current_build_dir() / (test_name + '.p')

        # Meson only auto-creates the private dirs of compiled targets, so
        # the working directories of the Python examples are made at setup
        run_command('mkdir', '-p', output_dir, check: true)

        test('cleanup_script_' + test_name, cleanup_script,
                workdir: output_dir,
                suite : testSuite,
                is_parallel : false,
                priority : 2)

        test(test_name, py,
            args : [files('pytracr' / ex + '.py')],
            env : ['PYTHONPATH=' + pytracr_build_dir],
            workdir: output_dir,
            suite : testSuite,
            is_parallel : false,
            priority : 1)

        foreach format : ['paraver', 'perfetto', 'dump']
            test('tracr_process_' + format + '_' + test_name, tracr_process,
                workdir: meson.current_build_dir() / '../postprocessing',
                args : [ output_dir / 'tracr', format],
                suite : testSuite,
                is_parallel : false,
                priority : 0)
        endforeach
    endforeach
endif
build_runner = get_option('runner')

runnerlib_sources = [ 'settings.c',
		      'job_list.c',
		      'executor.c',
		      'kmemleak.c',
		      'resultgen.c',
		      lib_version,
		    ]

runner_sources = [ 'runner.c' ]
resume_sources = [ 'resume.c' ]
results_sources = [ 'results.c' ]
decoder_sources = [ 'decoder.c' ]
runner_test_sources = [ 'runner_tests.c' ]
runner_json_test_sources = [ 'runner_json_tests.c' ]
runner_kmemleak_test_sources = [ 'runner_kmemleak_test.c' ]

jsonc = dependency('json-c', required: build_runner)
runner_deps = [jsonc, glib]
runner_c_args = []

liboping = dependency('liboping', required: get_option('oping'))
if liboping.found()
	runner_deps += liboping
	runner_c_args += '-DHAVE_OPING=1'
endif

if jsonc.found() and build_tests
	subdir('testdata')

	runnerlib = static_library('igt_runner', runnerlib_sources,
				   include_directories : inc,
				   c_args : runner_c_args,
				   dependencies : runner_deps)

	runner = executable('igt_runner', runner_sources,
			    link_with : runnerlib,
			    install : true,
			    install_dir : bindir,
			    install_rpath : bindir_rpathdir,
			    dependencies : igt_deps)

	resume = executable('igt_resume', resume_sources,
			    link_with : runnerlib,
			    install : true,
			    install_dir : bindir,
			    install_rpath : bindir_rpathdir,
			    dependencies : igt_deps)

	results = executable('igt_results', results_sources,
			     link_with : runnerlib,
			     install : true,
			     install_dir : bindir,
			     install_rpath : bindir_rpathdir,
			     dependencies : igt_deps)

	decoder = executable('igt_comms_decoder', decoder_sources,
			     link_with : runnerlib,
			     install : true,
			     install_dir : bindir,
			     install_rpath : bindir_rpathdir,
			     dependencies : igt_deps)

	runner_test = executable('runner_test', runner_test_sources,
				 c_args : '-DTESTDATA_DIRECTORY="@0@"'.format(testdata_dir),
				 link_with : runnerlib,
				 install : false,
				 dependencies : [igt_deps, jsonc])
	test('runner', runner_test, timeout : 300)

	runner_json_test = executable('runner_json_test', runner_json_test_sources,
				      c_args : '-DJSON_TESTS_DIRECTORY="@0@"'.format(join_paths(meson.current_source_dir(), 'json_tests_data')),
				      link_with : runnerlib,
				      install : false,
				      dependencies : [igt_deps, jsonc])
	test('runner_json', runner_json_test, timeout : 300)

	runner_kmemleak_test = executable('runner_kmemleak_test',
				 runner_kmemleak_test_sources,
				 link_with : runnerlib,
				 install : false,
				 dependencies : [igt_deps])
	test('runner_kmemleak', runner_kmemleak_test, timeout : 300)

	build_info += 'Build test runner: true'
	if liboping.found()
		build_info += 'Build test runner with oping: true'
	endif
else
	build_info += 'Build test runner: false'
endif

lib_tests = [
	'igt_assert',
	'igt_abort',
	'igt_can_fail',
	'igt_can_fail_simple',
	'igt_conflicting_args',
	'igt_describe',
	'igt_dynamic_subtests',
	'igt_edid',
	'igt_exit_handler',
	'igt_facts',
	'igt_fork',
	'igt_fork_helper',
	'igt_hook',
	'igt_hook_integration',
        'igt_ktap_parser',
	'igt_list_only',
	'igt_invalid_subtest_name',
	'igt_nesting',
	'igt_no_exit',
	'igt_runnercomms_packets',
	'igt_segfault',
	'igt_simulation',
	'igt_stats',
	'igt_subtest_group',
	'igt_thread',
	'igt_types',
	'i915_perf_data_alignment',
]

lib_fail_tests = [
	'bad_subtest_type',
	'igt_no_subtest',
	'igt_simple_test_subtests',
	'igt_timeout',
]

lib_tests_deps = igt_deps

if chamelium.found()
	lib_deps += chamelium
	lib_tests += 'igt_audio'
endif

foreach lib_test : lib_tests
	exec = executable(lib_test, lib_test + '.c', install : false,
			dependencies : igt_deps)
	test('lib ' + lib_test, exec)
endforeach

foreach lib_test : lib_fail_tests
	exec = executable(lib_test, lib_test + '.c', install : false,
			dependencies : igt_deps)
	test('lib ' + lib_test, exec, should_fail : true)
endforeach

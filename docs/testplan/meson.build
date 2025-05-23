testplan_title = 'IGT test plans'

intel_testlist_dir = 'intel-ci-tests'

sphinx = find_program('sphinx-build', required: build_sphinx)
rst2html = find_program('rst2html-3', 'rst2html', required : false)
rst2pdf = find_program('rst2pdf', required: false)

stylesheet = join_paths(meson.current_source_dir(), 'testplan.css')

xe_test_config = join_paths(source_root, 'tests', 'intel', 'xe_test_config.json')
kms_test_config = join_paths(source_root, 'tests', 'intel', 'kms_test_config.json')
i915_test_config = join_paths(source_root, 'tests', 'intel', 'i915_test_config.json')

check_testlist = []
kms_check_testlist = []
if build_tests
	doc_dependencies = testlist_files
	# Check if documentation matches the actual tests and tests can run
	if not meson.is_cross_build()
		build_info += 'Will Check if documentation is in sync with testlist'
		check_testlist = [ '--check-testlist', '--igt-build-path', build_root ]

		if not chamelium.found()
			warning('WARNING: Will not check if documentation is in sync for KMS as chamelium is disabled')
		else
			kms_check_testlist = check_testlist
		endif
	else
		warning('WARNING: Will not check if documentation is in sync with testlist')
	endif
else
	doc_dependencies = []
endif

if build_xe
	test_dict = {
		'i915_tests': { 'input': i915_test_config, 'extra_args': check_testlist },
		'kms_tests': { 'input': kms_test_config, 'extra_args': kms_check_testlist },
		'xe_tests': { 'input': xe_test_config, 'extra_args': check_testlist }
	    }
else
	test_dict = {
	      'i915_tests': { 'input': i915_test_config, 'extra_args': check_testlist },
	      'kms_tests': { 'input': kms_test_config, 'extra_args': kms_check_testlist }
	    }
endif

testplans = []

foreach testplan, fields: test_dict
	rst = custom_target(testplan + '.rst',
			    build_by_default : true,
			    command : [ igt_doc_script, '--config', '@INPUT@', '--rest', '@OUTPUT@' ] + fields['extra_args'],
			    depends : doc_dependencies,
			    input : fields['input'],
			    output : testplan + '.rst'
			   )

	testplans += fields['input']

	if rst2html.found()
		custom_target(testplan + '.html',
			      build_by_default : true,
			      command : [ rst2html, '--stylesheet=' + stylesheet, '--field-name-limit=0', '@INPUT@', '@OUTPUT@' ],
			      input : rst,
			      output : testplan + '.html'
			     )
	endif
endforeach

custom_target(intel_testlist_dir,
	      build_by_default : true,
	      build_always_stale : true,
	      command : [ igt_doc_script, '--config', '@INPUT@', '--intelci-testlist', '@OUTPUT@' ],
	      depends : doc_dependencies,
	      input : testplans,
	      output : intel_testlist_dir,
	      install : true,
	      install_dir : libexecdir
	     )

if sphinx.found()
	if gen_rst_index.found()
		sphinx_out_dir = meson.current_build_dir()+ '/indexed_html'

		index_rst = custom_target('index.rst',
					  build_by_default : true,
					  command : [ gen_rst_index, testplan_title, test_dict.keys(), meson.current_build_dir()],
					  input : rst,
					  output : 'index.rst'
					 )

		custom_target('index.html',
			      build_by_default : true,
			      command : [ 'sphinx-build', '-c', meson.current_source_dir(),
					  meson.current_build_dir(), sphinx_out_dir],
			      input : index_rst,
			      output : 'index.html'
			     )
	endif

	if rst2pdf.found()
		sphinx_out_pdf = meson.current_build_dir() + '/pdf'

		custom_target('tests.pdf',
			      build_by_default : true,
			      command : [ 'sphinx-build', '-c', meson.current_source_dir(),
					  '-b', 'pdf',
					  '-D', 'version=' + meson.project_version(),
					  meson.current_build_dir(), sphinx_out_pdf],
			      input : index_rst,
			      output : 'tests.pdf'
			     )
	endif
endif

build_info += 'Build simple html testplan documentation: @0@'.format(rst2html.found())
build_info += 'Build indexed html testplan documentation: @0@'.format(sphinx.found())
build_info += 'Build pdf testplan documentation: @0@'.format(sphinx.found() and rst2pdf.found())

benchmark_progs = [
	'gem_blt',
	'gem_busy',
	'gem_create',
	'gem_exec_ctx',
	'gem_exec_fault',
	'gem_exec_nop',
	'gem_exec_reloc',
	'gem_exec_trace',
	'gem_latency',
	'gem_prw',
	'gem_set_domain',
	'gem_syslatency',
	'gem_userptr_benchmark',
	'gem_wsim',
	'intel_upload_blit_large',
	'intel_upload_blit_large_gtt',
	'intel_upload_blit_large_map',
	'intel_upload_blit_small',
	'kms_fb_stress',
	'kms_vblank',
	'prime_lookup',
	'vgem_mmap',
        'xe_blt',
	'xe_create',
	'xe_exec_ctx',
]

benchmarksdir = join_paths(libexecdir, 'benchmarks')

foreach prog : benchmark_progs
	executable(prog, prog + '.c',
		   install : true,
		   install_dir : benchmarksdir,
		   dependencies : igt_deps)
endforeach

lib_gem_exec_tracer = shared_module(
  'gem_exec_tracer',
  'gem_exec_tracer.c',
  dependencies : dlsym,
  include_directories : inc,
  install_dir : benchmarksdir,
  install: true)

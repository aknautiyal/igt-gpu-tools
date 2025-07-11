project('igt-gpu-tools', 'c',
	version : '2.1',
        default_options: [
          'warning_level=2',
          'c_std=gnu11',
          'b_ndebug=false',
          'buildtype=debugoptimized',
        ],
	license : 'MIT',
	meson_version : '>=0.52.1')

if get_option('b_ndebug') != 'false'
	error('Building without -Db_ndebug=false is not supported')
endif

# meson.source_root is marked as a future-deprecated feature
source_root = meson.current_source_dir()
build_root = meson.current_build_dir()

cc = meson.get_compiler('c')

# Also make sure that the user doesn't have -DNDEBUG defined in their config
if not cc.compiles(files('lib/check-ndebug.h'), args: get_option('c_args'))
	error('Building with NDEBUG defined is not supported')
endif

cc_args = [
	'-Wbad-function-cast',
	'-Wdeclaration-after-statement',
	'-Wformat=2',
# igt_assert(0) in switch statements triggers a bunch of this.
	'-Wimplicit-fallthrough=0',
	'-Wlogical-op',
	'-Wmissing-declarations',
	'-Wmissing-format-attribute',
	'-Wmissing-noreturn',
	'-Wmissing-prototypes',
	'-Wnested-externs',
	'-Wold-style-definition',
	'-Wpointer-arith',
	'-Wredundant-decls',
	'-Wshadow',
	'-Wstrict-prototypes',
	'-Wuninitialized',
	'-Wunused',

	'-Wno-clobbered',
	'-Wno-maybe-uninitialized',
	'-Wno-missing-field-initializers',
	'-Wno-pointer-arith',
	'-Wno-address-of-packed-member',
	'-Wno-sign-compare',
# Macros asserting on the range of their arguments triggers this.
	'-Wno-type-limits',
	'-Wno-unused-parameter',
	'-Wno-unused-result',

	'-Werror=address',
	'-Werror=array-bounds',
	'-Werror=dangling-pointer',
	'-Werror=implicit',
	'-Werror=init-self',
	'-Werror=int-conversion',
	'-Werror=int-to-pointer-cast',
	'-Werror=main',
	'-Werror=missing-braces',
	'-Werror=nonnull',
	'-Werror=pointer-to-int-cast',
	'-Werror=return-type',
	'-Werror=sequence-point',
	'-Werror=trigraphs',
	'-Werror=write-strings',
# Disable the memory allocating builtins as they may cause unexpected behavior
# with our framework. They *may* get optimized out in favor of a register or
# stack variable, making them effectively local. Local variables do not play
# well with longjmp which is heavily used by IGT framework.
	'-fno-builtin-malloc',
	'-fno-builtin-calloc',
# Required to use off64_t and ino64_t on musl libc
	'-D_LARGEFILE64_SOURCE=1',
]

foreach cc_arg : cc_args
  if cc.has_argument(cc_arg)
    add_global_arguments(cc_arg, language : 'c')
  endif
endforeach

build_chamelium = get_option('chamelium')
build_docs = get_option('docs')
build_tests = not get_option('tests').disabled()
build_xe = not get_option('xe_driver').disabled()
build_xe_eudebug = get_option('xe_eudebug').enabled()
build_vmtb = get_option('vmtb').enabled()
with_libdrm = get_option('libdrm_drivers')

build_info = ['Build type: ' + get_option('buildtype')]
build_info += 'Build Xe EU debugger test framework: @0@'.format(build_xe_eudebug)
build_info += 'Build VM Test Bench: @0@'.format(build_vmtb)

inc = include_directories('include', 'include/drm-uapi', 'include/drm-uapi-experimental', 'include/linux-uapi', 'lib', 'lib/stubs/syscalls', '.')

inc_for_gtkdoc = include_directories('lib')

config = configuration_data()

null_dep = dependency('', required : false)

libdrm_info = []
libdrm_intel = null_dep
libdrm_nouveau = null_dep
libdrm_amdgpu = null_dep

libdrm_version = '>=2.4.82'
libdrm = dependency('libdrm', version : libdrm_version)
if with_libdrm.contains('auto') or with_libdrm.contains('intel')
	libdrm_intel = dependency('libdrm_intel', version : libdrm_version, required : with_libdrm.contains('intel'))
	libdrm_info += 'intel'
endif
if with_libdrm.contains('auto') or with_libdrm.contains('nouveau')
	libdrm_nouveau = dependency('libdrm_nouveau', version : libdrm_version, required : with_libdrm.contains('nouveau'))
	libdrm_info += 'nouveau'
	if libdrm_nouveau.found()
		config.set('HAVE_LIBDRM_NOUVEAU', 1)
	endif
endif
if with_libdrm.contains('auto') or with_libdrm.contains('amdgpu')
	libdrm_amdgpu = dependency('libdrm_amdgpu', version : libdrm_version, required : with_libdrm.contains('amdgpu'))
	libdrm_info += 'amdgpu'
endif

build_info += 'With libdrm: ' + ','.join(libdrm_info)

pciaccess = dependency('pciaccess', version : '>=0.10')
libkmod = dependency('libkmod')
libprocps = dependency('libprocps', required : false)
libproc2 = dependency('libproc2', required : false)
if libprocps.found()
  config.set('HAVE_LIBPROCPS', 1)
elif libproc2.found()
  config.set('HAVE_LIBPROC2', 1)
  if libproc2.version().version_compare('>= 4.0.5')
    config.set('HAVE_LIBPROC2_POST_4_0_5_API', 1)
  endif
else
  error('Either libprocps or libproc2 is required')
endif

libunwind = dependency('libunwind', required : get_option('libunwind'))
if libunwind.found()
	config.set('HAVE_LIBUNWIND', 1)
endif
build_info += 'With libunwind: @0@'.format(libunwind.found())

libdw = dependency('libdw', required : true)
pixman = dependency('pixman-1', required : true)

valgrind = dependency('valgrind', required : get_option('valgrind'))
if valgrind.found()
	config.set('HAVE_VALGRIND', 1)
endif
build_info += 'Valgrind annotations: @0@'.format(valgrind.found())

cairo = dependency('cairo', version : '>1.12.0', required : true)
libudev = dependency('libudev', required : true)
glib = dependency('glib-2.0', required : true)

xmlrpc = dependency('xmlrpc', required : false)
xmlrpc_util = dependency('xmlrpc_util', required : false)
xmlrpc_client = dependency('xmlrpc_client', required : false)

xmlrpc_cmd = find_program('xmlrpc-c-config', required : false)
if not xmlrpc.found() and xmlrpc_cmd.found()
	libs_cmd = run_command(xmlrpc_cmd, 'client', '--libs')
	cflags_cmd = run_command(xmlrpc_cmd, 'client', '--cflags')

	if libs_cmd.returncode() == 0 and cflags_cmd.returncode() == 0
		xmlrpc = declare_dependency(compile_args: cflags_cmd.stdout().strip().split(),
					    link_args : libs_cmd.stdout().strip().split())
		xmlrpc_util = declare_dependency()
		xmlrpc_client = declare_dependency()
	endif
endif

if build_chamelium.enabled() and not (xmlrpc.found() and xmlrpc_util.found() and xmlrpc_client.found())
	error('Chamelium build forced and required dependency xmlrpc not found')
endif

gsl = dependency('gsl', required : build_chamelium)
alsa = dependency('alsa', required : build_chamelium)
libcurl = dependency('libcurl', required : build_chamelium)

if xmlrpc.found() and xmlrpc_util.found() and xmlrpc_client.found() and gsl.found() and alsa.found() and libcurl.found()
	config.set('HAVE_CHAMELIUM', 1)
	chamelium = declare_dependency(dependencies : [
		xmlrpc,
		xmlrpc_util,
		xmlrpc_client,
		gsl,
		alsa,
	])
else
	chamelium = disabler()
endif

build_info += 'Build Chamelium test: @0@'.format(chamelium.found())

pthreads = dependency('threads')
math = cc.find_library('m')
realtime = cc.find_library('rt')
dlsym = cc.find_library('dl')
zlib = cc.find_library('z')

if cc.links('''
#include <stdint.h>
int main(void) {
  uint32_t x32 = 0;
  uint64_t x64 = 0;
  __atomic_load_n(&x32, __ATOMIC_SEQ_CST);
  __atomic_load_n(&x64, __ATOMIC_SEQ_CST);
  return 0;
}''', name : 'built-in atomics')
	libatomic = null_dep
else
	libatomic = cc.find_library('atomic')
endif

if cc.has_header('linux/kd.h')
	config.set('HAVE_LINUX_KD_H', 1)
endif
if cc.has_header('sys/kd.h')
	config.set('HAVE_SYS_KD_H', 1)
endif
if cc.has_header('libgen.h')
	config.set('HAVE_LIBGEN_H', 1)
endif
if cc.has_header('sys/io.h') and cc.has_function('outb', prefix: '#include <sys/io.h>')
	config.set('HAVE_SYS_IO_H', 1)
endif
if cc.links('''
#include <cpuid.h>
#include <stddef.h>

int main(void) {
  int eax, ebx, ecx, edx;
  if (__get_cpuid_max(0, NULL) < 4)
    return 0;
  __cpuid_count(4, 0, eax, ebx, ecx, edx);
}''', name : 'cpuid.h')
	config.set('HAVE_CPUID_H', 1)
endif
if cc.has_header_symbol('unistd.h', 'gettid', args : '-D_GNU_SOURCE')
	config.set('HAVE_GETTID', 1)
endif

if cc.has_member('struct sysinfo', 'totalram',
		prefix : '#include <sys/sysinfo.h>')
	config.set('HAVE_STRUCT_SYSINFO_TOTALRAM', 1)
endif

have = cc.has_function('memfd_create', prefix : '''#include <sys/mman.h>''', args : '-D_GNU_SOURCE')
config.set10('HAVE_MEMFD_CREATE', have)

add_project_arguments('-D_GNU_SOURCE', language : 'c')
add_project_arguments('-include', 'config.h', language : 'c')

# FEATURE_TEST_MACROS(7)
# performs lightweight overflow checks on quite a few libc functions
# requires -O optimizations
if  ['debugoptimized', 'release', 'minsize'].contains(get_option('buildtype'))
	add_project_arguments('-D_FORTIFY_SOURCE=2', language : 'c')
endif

config.set('PACKAGE_NAME', meson.project_name())
config.set_quoted('PACKAGE_VERSION', meson.project_version())
config.set_quoted('PACKAGE', meson.project_name())
config.set('PACKAGE_STRING', meson.project_name() + ' ' + meson.project_version())
config.set_quoted('TARGET_CPU_PLATFORM', host_machine.cpu_family())

configure_file(output: 'config.h', configuration: config)

prefix = get_option('prefix')
bindir = get_option('bindir')
datadir = join_paths(get_option('datadir'), 'igt-gpu-tools')
includedir = get_option('includedir')
libdir = get_option('libdir')
libexecdir = join_paths(get_option('libexecdir'), 'igt-gpu-tools')
amdgpudir = join_paths(libexecdir, 'amdgpu')
msmdir = join_paths(libexecdir, 'msm')
panfrostdir = join_paths(libexecdir, 'panfrost')
v3ddir = join_paths(libexecdir, 'v3d')
vc4dir = join_paths(libexecdir, 'vc4')
vmwgfxdir = join_paths(libexecdir, 'vmwgfx')
mandir = get_option('mandir')
pkgconfigdir = join_paths(libdir, 'pkgconfig')
python3 = find_program('python3', required : true)

if get_option('use_rpath')
	# Set up runpath for the test executables towards libigt.so.
	# The path should be relative to $ORIGIN so the library is
	# still found properly even if installed to a path other than
	# prefix.

	# libdir and bindir are pathnames relative to prefix. meson
	# enforces this.

	# 1. Start from the executable.
	# 2. Executables are installed in certain dir. Add a .. for each
	#    directory name in it.
	# 3. Add relative path to libdir.

	bindir_rpathdir = '$ORIGIN'
	foreach p : bindir.split('/')
		bindir_rpathdir = join_paths(bindir_rpathdir, '..')
	endforeach
	bindir_rpathdir = join_paths(bindir_rpathdir, libdir)

	libexecdir_rpathdir = '$ORIGIN'
	foreach p : libexecdir.split('/')
		libexecdir_rpathdir = join_paths(libexecdir_rpathdir, '..')
	endforeach
	libexecdir_rpathdir = join_paths(libexecdir_rpathdir, libdir)

	amdgpudir_rpathdir = '$ORIGIN'
	foreach p : amdgpudir.split('/')
		amdgpudir_rpathdir = join_paths(amdgpudir_rpathdir, '..')
	endforeach
	amdgpudir_rpathdir = join_paths(amdgpudir_rpathdir, libdir)

	msm_rpathdir = '$ORIGIN'
	foreach p : msmdir.split('/')
		msm_rpathdir = join_paths(msm_rpathdir, '..')
	endforeach
	msm_rpathdir = join_paths(msm_rpathdir, libdir)

	panfrost_rpathdir = '$ORIGIN'
	foreach p : panfrostdir.split('/')
		panfrost_rpathdir = join_paths(panfrost_rpathdir, '..')
	endforeach
	panfrost_rpathdir = join_paths(panfrost_rpathdir, libdir)

	v3d_rpathdir = '$ORIGIN'
	foreach p : v3ddir.split('/')
		v3d_rpathdir = join_paths(v3d_rpathdir, '..')
	endforeach
	v3d_rpathdir = join_paths(v3d_rpathdir, libdir)

	vc4_rpathdir = '$ORIGIN'
	foreach p : vc4dir.split('/')
		vc4_rpathdir = join_paths(vc4_rpathdir, '..')
	endforeach
	vc4_rpathdir = join_paths(vc4_rpathdir, libdir)

	vmwgfx_rpathdir = '$ORIGIN'
	foreach p : vmwgfxdir.split('/')
		vmwgfx_rpathdir = join_paths(vmwgfx_rpathdir, '..')
	endforeach
	vmwgfx_rpathdir = join_paths(vmwgfx_rpathdir, libdir)
else
	bindir_rpathdir = ''
	libexecdir_rpathdir = ''
	amdgpudir_rpathdir = ''
	msm_rpathdir = ''
	panfrost_rpathdir = ''
	v3d_rpathdir = ''
	vc4_rpathdir = ''
	vmwgfx_rpathdir = ''
endif

build_testplan = get_option('testplan')
build_sphinx = get_option('sphinx')

subdir('scripts')

subdir('lib')
if build_tests
	subdir('tests')
else
	xe_test_config = ''
endif
build_info += 'Build tests: @0@'.format(build_tests)

subdir('benchmarks')
subdir('tools')
subdir('runner')
if libdrm_intel.found()
	subdir('assembler')
endif
subdir('overlay')
subdir('man')
subdir('docs')

message('Build options')
message('=============')
foreach str : build_info
	message(str)
endforeach

if cairo.version().version_compare('<1.17.2')
	if pixman.version().version_compare('<0.36.0')
		warning('Pixman < 0.36.0 found, cannot test HDR formats')
	endif
	warning('Cairo < 1.17.2 found, cannot test HDR formats')
elif pixman.version().version_compare('<0.36.0')
	# Cairo 1.17.2 requires 0.36.0 to compile, but somehow it went missing?
	error('Cairo with floating point support found, but pixman version too old')
endif

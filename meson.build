project('OFSExtractor', 'c', version : '1.0', default_options : ['warning_level=3'])

incdir = include_directories('include')

c = meson.get_compiler('c')
m_dep = c.find_library('m', required : false)

date = run_command('date', '+%b %d %Y %T %Z').stdout().strip()
cdata = configuration_data()
cdata.set('date', date)
compiledate = configure_file(
    input : 'compiledate.h.in',
    output : 'compiledate.h',
    configuration : cdata
)

version = vcs_tag(
  command: ['git', 'describe', '--tags', '--dirty=+'],
  input: 'version.h.in',
  output: 'version.h')

cpu_family = target_machine.cpu_family()
system = target_machine.system()

if system == 'windows'
    if cpu_family == 'x86'
        binary_name = meson.project_name() + '32'
    endif
    
    if cpu_family == 'x86_64'
        binary_name = meson.project_name() + '64'
    endif
else
    binary_name = meson.project_name()
endif

executable(
    binary_name,
    version,
    compiledate,

    'src/main.c',
    'src/util.c',
    'src/3dplanes.c',

    include_directories: incdir,

    dependencies : m_dep,

    install: true
)

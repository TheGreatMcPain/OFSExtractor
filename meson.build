project('OFSExtractor', 'c', version : '1.1-beta1', default_options : ['warning_level=3'])

incdir = include_directories('include')

date = run_command('env', 'TZ=UTC', 'git', 'show', '-s', '--format=%cd',
                   '--date=format-local:%b %d %Y %T UTC').stdout().strip()
cdata = configuration_data()
cdata.set('date', date)
commitdate = configure_file(
    input : 'commitdate.h.in',
    output : 'commitdate.h',
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
    commitdate,

    'src/main.c',
    'src/util.c',
    'src/3dplanes.c',

    include_directories: incdir,

    install: true
)

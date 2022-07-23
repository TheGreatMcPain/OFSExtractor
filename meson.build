project('OFSExtractor', 'c',
    version : '1.1-beta2',
    default_options : ['warning_level=3', 'b_lto=true']
)

# Get latest git commit date, then add it to 'commitdate.h'
date = run_command('env', 'TZ=UTC', 'git', 'show', '-s', '--format=%cd',
                   '--date=format-local:%b %d %Y %T UTC').stdout().strip()
cdata = configuration_data()
cdata.set('date', date)
commitdate = configure_file(
    input : 'commitdate.h.in',
    output : 'commitdate.h',
    configuration : cdata
)

# Get project version from git tags, then add it to 'version.h'
version = vcs_tag(
  command: ['git', 'describe', '--tags', '--dirty=+'],
  input: 'version.h.in',
  output: 'version.h'
)

# For windows binaries append the architecture bits to the
# binary's name.
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

# Source files
incdir = include_directories('include')
src_files = files(
    [
        'src/main.c',
        'src/util.c',
        'src/3dplanes.c'
    ]
)

executable(
    binary_name,
    version,
    commitdate,
    src_files,
    include_directories: incdir,
    install: true
)

project('OFSExtractor', 'c', version : '1.0', default_options : ['warning_level=3'])

incdir = include_directories('include')

c = meson.get_compiler('c')
m_dep = c.find_library('m', required : false)

date = run_command('date', '+%b %d %Y %T %Z').stdout().strip()
cdata = configuration_data()
cdata.set('date', date)
configure_file(
    input : 'compiledate.h.in',
    output : 'compiledate.h',
    configuration : cdata
)

executable(
    meson.project_name(),

    'src/main.c',
    'src/util.c',
    'src/3dplanes.c',

    include_directories: incdir,

    dependencies : m_dep,

    install: true
)


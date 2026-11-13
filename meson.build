project('skim', 'c',
  version: '0.2.0-rc.1',
  license: 'MIT',
  default_options: [
    'buildtype=release',
    'optimization=2',
    'c_std=c23',
    'default_library=both',
    'warning_level=2',
  ]
)

public_inc = include_directories('.')
private_inc = include_directories('include')
cc = meson.get_compiler('c')
math_dep = cc.find_library('m', required: false)

static_api_args = ['-DSKIM_STATIC']
library_kind = get_option('default_library')
libskim_api_args = library_kind == 'static' ? static_api_args : ['-DSKIM_BUILDING_DLL']
skim_dep_api_args = library_kind == 'static' ? static_api_args : []

skim_sources = files(
  'src/chars.c',
  'src/lex.c',
  'src/parse/class.c',
  'src/parse/expression.c',
  'src/parse/function.c',
  'src/parse/module.c',
  'src/parse/parse.c',
  'src/parse/statement.c',
  'src/parse/type.c',
  'src/print/class.c',
  'src/print/enum.c',
  'src/print/literal.c',
  'src/print/module.c',
  'src/print/namespace.c',
  'src/print/number.c',
  'src/print/print.c',
  'src/skim.c',
  'src/str.c',
)

libskim = library('skim',
  skim_sources,
  include_directories: [public_inc, private_inc],
  dependencies: math_dep,
  c_args: ['-fvisibility=hidden'] + libskim_api_args,
  install: true,
)

skim_dep = declare_dependency(
  link_with: libskim,
  include_directories: public_inc,
  dependencies: math_dep,
  compile_args: skim_dep_api_args,
)

libskim_static = static_library('skim_static',
  skim_sources,
  include_directories: [public_inc, private_inc],
  dependencies: math_dep,
  c_args: ['-fvisibility=hidden'] + static_api_args,
)

skim_cli_dep = declare_dependency(
  link_with: libskim_static,
  include_directories: public_inc,
  dependencies: math_dep,
  compile_args: static_api_args,
)

skim_cli_link_args = cc.get_supported_link_arguments(
  '-Wl,-dead_strip',
  '-Wl,-no_data_const',
  '-Wl,-x',
  '-Wl,--gc-sections',
  '-Wl,--strip-all',
)

skim_cli = executable('skim',
  files('tools/skim.c'),
  dependencies: skim_cli_dep,
  include_directories: private_inc,
  c_args: ['-DSKIM_VERSION="' + meson.project_version() + '"'],
  link_args: skim_cli_link_args,
  install: true,
)

skim_bench = executable('skim_bench',
  files('tests/bench.c'),
  dependencies: skim_cli_dep,
  build_by_default: false,
)

install_headers('skim.h')

pkg = import('pkgconfig')
pkg.generate(libskim,
  name: 'skim',
  filebase: 'skim',
  description: 'Small TypeScript type stripper for C projects',
  version: meson.project_version(),
)

if get_option('tests')
  skim_context_test = executable('skim_context_test',
    files('tests/context.c'),
    dependencies: skim_dep,
    include_directories: private_inc,
  )

  node = find_program('node', required: false)
  if node.found()
    test('fixtures', node,
      args: files('tests/run.cjs'),
      env: ['SKIM_BIN=' + skim_cli.full_path()],
    )
  endif

  test_fixtures = files(
    'tests/fixtures/annotations.ts',
    'tests/fixtures/class_modifiers.ts',
    'tests/fixtures/enum.ts',
    'tests/fixtures/enum_refs.ts',
    'tests/fixtures/decorated_async.ts',
    'tests/fixtures/export_named.ts',
    'tests/fixtures/generics.ts',
    'tests/fixtures/import_equals.cjs.ts',
    'tests/fixtures/mixed_default_import.ts',
    'tests/fixtures/mixed_import.ts',
    'tests/fixtures/namespace.ts',
    'tests/fixtures/namespace_exports.ts',
    'tests/fixtures/non_null_optional.ts',
    'tests/fixtures/regex_literals.ts',
    'tests/fixtures/parameter_property.ts',
    'tests/fixtures/parameter_property_super.ts',
  )

  test('context', skim_context_test, args: test_fixtures)

  threads_dep = dependency('threads', required: false)
  if threads_dep.found() and host_machine.system() != 'windows'
    skim_threads_test = executable('skim_threads_test',
      files('tests/threads.c'),
      dependencies: [skim_dep, threads_dep],
    )
    test('threads', skim_threads_test, args: test_fixtures)
  endif
endif

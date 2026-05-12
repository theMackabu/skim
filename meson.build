project('skim', 'c',
  version: '0.0.1',
  license: 'MIT',
  default_options: [
    'c_std=c23',
    'default_library=both',
    'warning_level=2',
  ]
)

public_inc = include_directories('.')
private_inc = include_directories('include')

skim_sources = files(
  'src/ast.c',
  'src/emit.c',
  'src/emit_fast.c',
  'src/lexer.c',
  'src/module_markers.c',
  'src/parser.c',
  'src/scan.c',
  'src/skim.c',
  'src/transform.c',
  'src/typescript/annotations.c',
  'src/typescript/class.c',
  'src/typescript/enum.c',
  'src/typescript/module.c',
  'src/typescript/namespace.c',
)

libskim = library('skim',
  skim_sources,
  include_directories: [public_inc, private_inc],
  c_args: ['-fvisibility=hidden'],
  install: true,
)

skim_dep = declare_dependency(
  link_with: libskim,
  include_directories: public_inc,
)

skim_cli = executable('skim',
  files('tools/skim.c'),
  dependencies: skim_dep,
  include_directories: private_inc,
  install: true,
)

install_headers('skim.h')

pkg = import('pkgconfig')
pkg.generate(libskim,
  name: 'skim',
  filebase: 'skim',
  description: 'Small TypeScript type stripper for C projects',
  version: meson.project_version(),
)

node = find_program('node', required: false)
if node.found()
  test('fixtures', node,
    args: files('tests/run.cjs'),
    env: ['SKIM_BIN=' + skim_cli.full_path()],
  )
endif

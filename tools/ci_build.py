"""Build, test and package the standalone Windows provider."""
import argparse
import json
from pathlib import Path
import sys
from release_support import build_info, cmake_info, download, output, package_component, run, version

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', type=version)
    parser.add_argument('--revision-asset', action='store_true',
                        help='Use a source-revision suffix for an additive asset of an existing preview')
    parser.add_argument('--ninja', action='store_true', help='Use Ninja from a configured compiler environment')
    args = parser.parse_args()
    source_revision = output('git', 'rev-parse', 'HEAD')
    tag = args.version or 'v0.0.1-ci.' + source_revision[:12]
    lock = json.loads((ROOT / 'build-dependencies.json').read_text())
    deps = ROOT / 'out/ci-deps'
    download(lock['json']['header'], deps / 'json/include/nlohmann/json.hpp')
    license_path = download(lock['json']['license'], deps / 'json/LICENSE')
    build = ROOT / ('build/ci-ninja' if args.ninja else 'build/ci')
    generator = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'] if args.ninja else ['-A', 'x64']
    run('cmake', '-S', ROOT, '-B', build, *generator,
        '-DOSW_BUILD_SHARED=ON', '-DOSW_BUILD_TOOLS=ON', '-DOSW_BUILD_TESTS=ON',
        '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded', '-DOSW_JSON_INCLUDE_DIR=' + str(deps / 'json/include'))
    run('cmake', '--build', build, '--config', 'Release', '--parallel', '2')
    run('ctest', '--test-dir', build, '-C', 'Release', '--output-on-failure', '--timeout', '120')
    binary = build if args.ninja else build / 'Release'
    files = {'orbisteamworks.dll': (binary / 'orbisteamworks.dll').read_bytes(),
             'tools/osw-auth-helper.exe': (binary / 'osw-auth-helper.exe').read_bytes(),
             'include/orbisteamworks/osw.h': (ROOT / 'include/orbisteamworks/osw.h').read_bytes(),
             'LICENSE': (ROOT / 'LICENSE').read_bytes(), 'README.md': (ROOT / 'README.md').read_bytes(),
             'licenses/nlohmann-json.txt': license_path.read_bytes()}
    for doc in ('BUILDING.md', 'C_API.md', 'COMPANION_PROTOCOL.md', 'INTEGRATION.md'):
        files['docs/' + doc] = (ROOT / 'docs' / doc).read_bytes()
    info = build_info()
    info.update(cmake_info(build))
    info['dependencies'] = lock
    asset_suffix = '-' + source_revision[:12] if args.revision_asset else ''
    package_component(ROOT / 'dist' / f'orbisteamworks-{tag}{asset_suffix}-windows-x64.zip', files,
                      'orbisteamworks', tag, source_revision, 'MIT', info)


if __name__ == '__main__':
    main()

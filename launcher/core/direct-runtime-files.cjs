// Files required by Direct mode in every packaged installation.
module.exports = [
  'out/direct/axrb-direct.exe',
  'out/direct/art/axrb-bootstrap.dex',
  'out/direct/art/AOSP-NOTICE.xml.gz',
  ...['libandroid.so', 'libaxrbndk.so', 'libmediandk.so', 'libaxrbmndk.so', 'libaxrb_art_bridge.so', 'libOpenSLES.so'].map(name => `out/direct/art/lib64/${name}`),
  ...['libandroid.so', 'libvulkan.so', 'libEGL.so', 'libGLESv1_CM.so',
    'libGLESv2.so', 'libGLESv3.so', 'libaaudio.so', 'libOpenSLES.so',
    'libopenxr_runtime.so'].map(name => `out/direct/guest/lib64/${name}`),
  'out/direct/guest/axrb-systemdriver.apk',
  'scripts/run/run_direct_game.ps1',
  'direct/tools/make_sysroot.py',
  'direct/tools/prepare_art_session.py',
];

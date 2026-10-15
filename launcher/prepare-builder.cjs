const verify = require('./verify-release.cjs');

module.exports = async context => {
  // Modern 7-Zip auto-selects ARM64 for our guest ELF libraries. The NSIS
  // extractor silently skips that unsupported filter, even on a clean install.
  // Pin a filter supported by NSIS for both setup and portable EXE payloads.
  process.env.ELECTRON_BUILDER_7Z_FILTER = 'BCJ';
  await verify(context);
};

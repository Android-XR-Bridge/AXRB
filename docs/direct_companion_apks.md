# Explicit companion APKs for Direct sessions

Some owned Android games load initialization code from a separately supplied package. Direct sessions currently expose only the game and the legacy Oculus driver. This contribution adds opt-in, read-only companion APK mounting and package metadata registration, without bundling third-party packages, changing ownership state, or providing remote account services.

Validation will distinguish staging and Java build checks from a native runtime/headset test. The current local native build is blocked by the older installed C++ standard library; no Iron Man gameplay claim is made.

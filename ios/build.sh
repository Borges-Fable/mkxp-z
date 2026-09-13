#!/bin/bash
# Cross-compiles mkxp-z for arm64 iPhones and packs an unsigned, game-less .ipa.
# Needs a macOS host with Xcode, and ios/Dependencies already built (`make -C ios/Dependencies`).
#
# Output: ios/build/mkxp-z-unsigned.ipa
# The game goes into Payload/mkxp-z.app/Game/ afterwards; SideStore (or any
# signing tool) signs the finished .ipa.
set -euo pipefail
cd "$(dirname "$0")/.."

MIN_IOS=18.0
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
SDK_VERSION=$(xcrun --sdk iphoneos --show-sdk-version)
PREFIX="$PWD/ios/Dependencies/build-iphoneos-arm64"
OUT="$PWD/ios/build"
BUILD_NUMBER="${BUILD_NUMBER:-1}"

tool() { xcrun --sdk iphoneos -f "$1"; }
flags="'-arch', 'arm64', '-isysroot', '$SDK', '-miphoneos-version-min=$MIN_IOS'"

mkdir -p "$OUT"
cat > "$OUT/cross.ini" <<EOF
[binaries]
c = '$(tool clang)'
cpp = '$(tool clang++)'
objc = '$(tool clang)'
objcpp = '$(tool clang++)'
ar = '$(tool ar)'
strip = '$(tool strip)'
pkg-config = 'pkg-config'

[built-in options]
c_args = [$flags, '-I$PREFIX/include']
cpp_args = [$flags, '-I$PREFIX/include']
c_link_args = [$flags, '-L$PREFIX/lib']
cpp_link_args = [$flags, '-L$PREFIX/lib']

[properties]
needs_exe_wrapper = true
pkg_config_libdir = '$PREFIX/lib/pkgconfig'
sys_root = '$SDK'

[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'
EOF

export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=""

rm -rf "$OUT/meson"
meson setup "$OUT/meson" --cross-file "$OUT/cross.ini" \
  -Dios=true \
  -Dgfx_backend=gles \
  -Dshared_fluid=false \
  -Denable-https=false \
  -Dmri_libpath="$PREFIX/lib"
ninja -C "$OUT/meson"

APP="$OUT/Payload/mkxp-z.app"
rm -rf "$OUT/Payload"
mkdir -p "$APP/Game"
cp "$OUT/meson/mkxp-z" "$APP/mkxp-z"
"$(tool strip)" -x "$APP/mkxp-z"
cp ios/README-Game.txt "$APP/Game/README.txt"

cat > "$APP/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleExecutable</key><string>mkxp-z</string>
  <key>CFBundleIdentifier</key><string>io.github.borgesfable.mkxpz</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>mkxp-z</string>
  <key>CFBundleDisplayName</key><string>mkxp-z</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>2.4.2</string>
  <key>CFBundleVersion</key><string>$BUILD_NUMBER</string>
  <key>CFBundleSupportedPlatforms</key><array><string>iPhoneOS</string></array>
  <key>DTPlatformName</key><string>iphoneos</string>
  <key>DTPlatformVersion</key><string>$SDK_VERSION</string>
  <key>DTSDKName</key><string>iphoneos$SDK_VERSION</string>
  <key>MinimumOSVersion</key><string>$MIN_IOS</string>
  <key>LSRequiresIPhoneOS</key><true/>
  <key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
  <key>UIRequiredDeviceCapabilities</key><array><string>arm64</string></array>
  <key>UILaunchScreen</key><dict/>
  <key>UIRequiresFullScreen</key><true/>
  <key>UIStatusBarHidden</key><true/>
  <key>UIViewControllerBasedStatusBarAppearance</key><false/>
  <key>UISupportedInterfaceOrientations</key>
  <array><string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string></array>
  <key>UISupportedInterfaceOrientations~ipad</key>
  <array><string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string></array>
  <key>UIFileSharingEnabled</key><true/>
  <key>LSSupportsOpeningDocumentsInPlace</key><true/>
  <key>NSLocalNetworkUsageDescription</key><string>Connects to the game server on your network.</string>
  <key>ITSAppUsesNonExemptEncryption</key><false/>
</dict>
</plist>
EOF
plutil -lint "$APP/Info.plist"

rm -f "$OUT/mkxp-z-unsigned.ipa"
(cd "$OUT" && zip -qry mkxp-z-unsigned.ipa Payload)
ls -lh "$OUT/mkxp-z-unsigned.ipa"

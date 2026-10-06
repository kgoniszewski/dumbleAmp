#!/usr/bin/env bash
# Sign (Developer ID, hardened runtime), package, notarize and staple one plugin (Dumble SSS by default).
#
# Required environment:
#   DEVELOPER_ID_APP   e.g. "Developer ID Application: Jane Doe (TEAMID1234)"
#   NOTARY_PROFILE     keychain profile created once with:
#                        xcrun notarytool store-credentials <profile> --apple-id <id> --team-id <team> --password <app-specific>
#                      (or: --key <AuthKey.p8> --key-id <id> --issuer <uuid> for an App Store Connect API key)
# Optional:
#   BUILD_DIR          default: build
#   OUT_DIR            default: dist
#   TARGET / PRODUCT   CMake target and product name; default DumbleSSS / "Dumble SSS",
#                      for the AC30: TARGET=VoxAC30C2 PRODUCT=AC30C2
set -euo pipefail

: "${DEVELOPER_ID_APP:?set DEVELOPER_ID_APP}"
: "${NOTARY_PROFILE:?set NOTARY_PROFILE}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
OUT_DIR="${OUT_DIR:-$ROOT/dist}"
TARGET="${TARGET:-DumbleSSS}"
PRODUCT="${PRODUCT:-Dumble SSS}"
ART="$BUILD_DIR/${TARGET}_artefacts/Release"
APP="$ART/Standalone/$PRODUCT.app"
AU="$ART/AU/$PRODUCT.component"
VERSION="$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$APP/Contents/Info.plist")"
DMG="$OUT_DIR/$TARGET-$VERSION.dmg"
STAGE="$OUT_DIR/stage-$TARGET"

[[ -d "$APP" && -d "$AU" ]] || { echo "build Release first: cmake --build $BUILD_DIR --config Release" >&2; exit 1; }

echo "==> codesign"
codesign --force --timestamp --options runtime --sign "$DEVELOPER_ID_APP" \
         --entitlements "$ROOT/resources/Standalone.entitlements" "$APP"
codesign --force --timestamp --options runtime --sign "$DEVELOPER_ID_APP" "$AU"
codesign --verify --strict --deep --verbose=2 "$APP"
codesign --verify --strict --deep --verbose=2 "$AU"

echo "==> package"
rm -rf "$STAGE" "$DMG" && mkdir -p "$STAGE"
cp -R "$APP" "$AU" "$STAGE/"
cat > "$STAGE/INSTALL.txt" <<TXT
$PRODUCT $VERSION
- $PRODUCT.app        -> /Applications
- $PRODUCT.component  -> ~/Library/Audio/Plug-Ins/Components
TXT
hdiutil create -volname "$PRODUCT $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
rm -rf "$STAGE"
codesign --force --timestamp --sign "$DEVELOPER_ID_APP" "$DMG"

echo "==> notarize"
xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait

echo "==> staple"
xcrun stapler staple "$DMG"
spctl --assess --type open --context context:primary-signature --verbose "$DMG"

echo "done: $DMG"

#!/usr/bin/env bash
# Sign (Developer ID, hardened runtime), package, notarize and staple Dumble SSS.
#
# Required environment:
#   DEVELOPER_ID_APP   e.g. "Developer ID Application: Jane Doe (TEAMID1234)"
#   NOTARY_PROFILE     keychain profile created once with:
#                        xcrun notarytool store-credentials <profile> --apple-id <id> --team-id <team> --password <app-specific>
#                      (or: --key <AuthKey.p8> --key-id <id> --issuer <uuid> for an App Store Connect API key)
# Optional:
#   BUILD_DIR          default: build
#   OUT_DIR            default: dist
set -euo pipefail

: "${DEVELOPER_ID_APP:?set DEVELOPER_ID_APP}"
: "${NOTARY_PROFILE:?set NOTARY_PROFILE}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
OUT_DIR="${OUT_DIR:-$ROOT/dist}"
ART="$BUILD_DIR/DumbleSSS_artefacts/Release"
APP="$ART/Standalone/Dumble SSS.app"
AU="$ART/AU/Dumble SSS.component"
VERSION="$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$APP/Contents/Info.plist")"
DMG="$OUT_DIR/DumbleSSS-$VERSION.dmg"

[[ -d "$APP" && -d "$AU" ]] || { echo "build Release first: cmake --build $BUILD_DIR --config Release" >&2; exit 1; }

echo "==> codesign"
codesign --force --timestamp --options runtime --sign "$DEVELOPER_ID_APP" \
         --entitlements "$ROOT/resources/Standalone.entitlements" "$APP"
codesign --force --timestamp --options runtime --sign "$DEVELOPER_ID_APP" "$AU"
codesign --verify --strict --deep --verbose=2 "$APP"
codesign --verify --strict --deep --verbose=2 "$AU"

echo "==> package"
rm -rf "$OUT_DIR" && mkdir -p "$OUT_DIR/stage"
cp -R "$APP" "$AU" "$OUT_DIR/stage/"
cat > "$OUT_DIR/stage/INSTALL.txt" <<TXT
Dumble SSS $VERSION
- Dumble SSS.app        -> /Applications
- Dumble SSS.component  -> ~/Library/Audio/Plug-Ins/Components
TXT
hdiutil create -volname "Dumble SSS $VERSION" -srcfolder "$OUT_DIR/stage" -ov -format UDZO "$DMG"
codesign --force --timestamp --sign "$DEVELOPER_ID_APP" "$DMG"

echo "==> notarize"
xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait

echo "==> staple"
xcrun stapler staple "$DMG"
spctl --assess --type open --context context:primary-signature --verbose "$DMG"

echo "done: $DMG"

#!/usr/bin/env bash
# Packs this folder into a .vsix without needing Node/vsce, then installs it.
set -euo pipefail
cd "$(dirname "$0")"
out="$PWD/wren-local.vsix"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/extension"
cp -r package.json language-configuration.json syntaxes "$tmp/extension/"
cat > "$tmp/extension.vsixmanifest" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="wren-local" Version="0.0.1" Publisher="local"/>
    <DisplayName>Wren (local)</DisplayName>
    <Description xml:space="preserve">Syntax highlighting for Wren.</Description>
    <Categories>Programming Languages</Categories>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="^1.60.0"/>
    </Properties>
  </Metadata>
  <Installation><InstallationTarget Id="Microsoft.VisualStudio.Code"/></Installation>
  <Dependencies/>
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true"/>
  </Assets>
</PackageManifest>
XML
cat > "$tmp/[Content_Types].xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension=".json" ContentType="application/json"/>
  <Default Extension=".vsixmanifest" ContentType="text/xml"/>
</Types>
XML
rm -f "$out"
(cd "$tmp" && zip -qr "$out" .)
code --install-extension "$out" --force

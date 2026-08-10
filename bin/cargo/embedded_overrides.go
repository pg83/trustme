package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
)

// Set by build.py through Go linker variables. Keeping the encoded source in
// the executable makes cargo independent of repository files at runtime.
var embeddedManifestOverridesBase64 string
var embeddedScriptOverridesBase64 string

var embeddedManifestOverrides string
var embeddedScriptOverrides map[string]string

const stdShimPackage = "mrustc_standard_library"

func init() {
	embeddedManifestOverrides = decodeEmbedded(
		"manifest overrides", embeddedManifestOverridesBase64,
	)
	embeddedScriptOverrides = map[string]string{}

	encoded := decodeEmbedded("build-script overrides", embeddedScriptOverridesBase64)
	if encoded != "" {
		if err := json.Unmarshal([]byte(encoded), &embeddedScriptOverrides); err != nil {
			panic(fmt.Sprintf("invalid embedded build-script overrides: %v", err))
		}
	}
}

func decodeEmbedded(name, encoded string) string {
	if encoded == "" {
		return ""
	}

	data, err := base64.StdEncoding.DecodeString(encoded)
	if err != nil {
		panic(fmt.Sprintf("invalid embedded %s: %v", name, err))
	}

	return string(data)
}

func buildScriptOverride(context *BuildContext, pkg *Package) (string, bool) {
	if !context.toolchainOverrides {
		return "", false
	}

	output, ok := embeddedScriptOverrides[pkg.name]
	return output, ok
}

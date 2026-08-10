package main

import "testing"

func TestEmbeddedOverridesAreScopedToStdBuild(t *testing.T) {
	oldManifest := embeddedManifestOverrides
	oldScripts := embeddedScriptOverrides
	defer func() {
		embeddedManifestOverrides = oldManifest
		embeddedScriptOverrides = oldScripts
	}()

	embeddedManifestOverrides = "[delete]\n'library/std' = ['dependencies.core']\n"
	embeddedScriptOverrides = map[string]string{"libc": "cargo:rustc-cfg=test\n"}
	libc := &Package{name: "libc"}
	external := &BuildContext{}
	toolchain := &BuildContext{toolchainOverrides: true}

	if _, ok := buildScriptOverride(external, libc); ok {
		t.Fatal("embedded build-script override leaked into an external build")
	}

	if output, ok := buildScriptOverride(toolchain, libc); !ok || output == "" {
		t.Fatal("embedded build-script override was not enabled for the std build")
	}

	if entries := loadManifestOverrides(false).entries; len(entries) != 0 {
		t.Fatalf("embedded manifest overrides leaked into an external build: %#v", entries)
	}

	if entries := loadManifestOverrides(true).entries; len(entries) != 1 {
		t.Fatalf("embedded manifest overrides were not enabled for the std build: %#v", entries)
	}
}

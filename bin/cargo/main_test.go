package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestCargoBuildCLI(t *testing.T) {
	opts := parseBuildOptions("test", []string{
		"--release",
		"--manifest-path", "some/Cargo.toml",
		"--target-dir", "out",
		"--jobs", "7",
		"--features", "a,b c",
		"--no-default-features",
		"--workspace",
		"--exclude", "skip",
		"--bin", "tool",
		"--no-run",
		"-Zvendor-dir=vendor",
		"-Zlib-search=std",
		"--", "--exact", "case",
	})

	if opts.profile != "release" || opts.jobs != 7 || opts.manifestPath != "some/Cargo.toml" {
		t.Fatalf("basic options not parsed: %#v", opts)
	}

	if len(opts.features) != 3 || opts.features[2] != "c" {
		t.Fatalf("features not parsed: %#v", opts.features)
	}

	if opts.vendorDir != "vendor" || len(opts.libSearch) != 1 || opts.libSearch[0] != "std" {
		t.Fatalf("trustme options not parsed: %#v", opts)
	}

	if len(opts.testArgs) != 2 || opts.testArgs[0] != "--exact" {
		t.Fatalf("test arguments not parsed: %#v", opts.testArgs)
	}

	if !opts.workspaceAll || len(opts.excludePackages) != 1 {
		t.Fatalf("workspace options not parsed: %#v", opts)
	}
}

func TestCargoNestedToolchainPaths(t *testing.T) {
	t.Setenv(trustmeCargoVendorDir, "/vendor")
	t.Setenv(trustmeCargoLibSearch, "/host/lib"+string(os.PathListSeparator)+"/target/lib")

	opts := parseBuildOptions("check", nil)
	if opts.vendorDir != "/vendor" || len(opts.libSearch) != 2 || opts.libSearch[1] != "/target/lib" {
		t.Fatalf("nested toolchain paths not inherited: %#v", opts)
	}
}

func TestCargoTestRuntimePackageContext(t *testing.T) {
	dir := t.TempDir()
	manifest := filepath.Join(dir, "Cargo.toml")
	pkg := &Package{
		dir: dir, manifestPath: manifest, name: "runtime-env",
		version: Version{major: 1, minor: 2, patch: 3},
	}
	builder := &Builder{context: &BuildContext{
		root: pkg,
		opts: BuildOptions{command: "test", targetDir: filepath.Join(dir, "target")},
	}}
	script := filepath.Join(dir, "check-env.sh")
	writeTestFile(t, script, `#!/bin/sh
test "$PWD" = "$1"
test "$CARGO_MANIFEST_DIR" = "$1"
test "$CARGO_MANIFEST_PATH" = "$1/Cargo.toml"
test "$CARGO_PKG_NAME" = runtime-env
test "$CARGO_PKG_VERSION" = 1.2.3
`)
	if err := os.Chmod(script, 0o755); err != nil {
		t.Fatal(err)
	}

	builder.runTest(script, []string{dir})
}

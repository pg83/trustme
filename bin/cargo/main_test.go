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

func TestAManifestPathIsRefusedAsCargoRefusesIt(t *testing.T) {
	dir := t.TempDir()
	cwd, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}
	if err := os.Chdir(dir); err != nil {
		t.Fatal(err)
	}
	defer os.Chdir(cwd)
	if err := os.MkdirAll(filepath.Join("dir", "Cargo.toml"), 0o755); err != nil {
		t.Fatal(err)
	}

	for _, want := range []struct{ argument, message string }{
		{"foo", "the manifest-path must be a path to a Cargo.toml file"},
		{"foo/Cargo.toml", "manifest path `foo/Cargo.toml` does not exist"},
		{"dir/Cargo.toml", "manifest path `dir/Cargo.toml` is a directory but expected a file"},
	} {
		exc := try(func() { requestedManifest(want.argument) })
		if exc == nil || exc.error() != want.message {
			t.Fatalf("--manifest-path %s: %v, want %q", want.argument, exc, want.message)
		}
	}
}

func TestTheVersionIsCargosOwn(t *testing.T) {
	if got := versionLine(); got != "cargo 1.90.0 (840b83a10 2025-07-30)" {
		t.Fatalf("cargo -V = %q", got)
	}
}

func TestLocateProjectFindsThePackageAndItsWorkspace(t *testing.T) {
	root := t.TempDir()
	member := filepath.Join(root, "member")
	if err := os.MkdirAll(filepath.Join(member, "src"), 0o755); err != nil {
		t.Fatal(err)
	}
	files := map[string]string{
		filepath.Join(root, "Cargo.toml"):   "[workspace]\nmembers = [\"member\"]\n",
		filepath.Join(member, "Cargo.toml"): "[package]\nname = \"member\"\nversion = \"0.1.0\"\n",
	}
	for path, text := range files {
		if err := os.WriteFile(path, []byte(text), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	manifest := filepath.Join(member, "Cargo.toml")

	if got := locateProject(manifest, false); got != manifest {
		t.Fatalf("locate-project = %q, want %q", got, manifest)
	}
	if got := locateProject(manifest, true); got != filepath.Join(root, "Cargo.toml") {
		t.Fatalf("locate-project --workspace = %q", got)
	}
}

package main

import "testing"

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
		t.Fatalf("mrustc options not parsed: %#v", opts)
	}

	if len(opts.testArgs) != 2 || opts.testArgs[0] != "--exact" {
		t.Fatalf("test arguments not parsed: %#v", opts.testArgs)
	}

	if !opts.workspaceAll || len(opts.excludePackages) != 1 {
		t.Fatalf("workspace options not parsed: %#v", opts)
	}
}

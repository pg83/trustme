package main

import (
	"archive/tar"
	"bytes"
	"io"
	"os"
	"path/filepath"
	"reflect"
	"testing"
	"time"

	"github.com/klauspost/compress/zstd"
)

func TestTarZstdIsSelfContainedAndReproducible(t *testing.T) {
	root := t.TempDir()
	archiveDir := t.TempDir()

	writeArchiveTestFile(t, filepath.Join(root, "z-last"), "last", 0o644)
	writeArchiveTestFile(t, filepath.Join(root, "bin", "tool"), "tool", 0o755)
	writeArchiveTestFile(t, filepath.Join(root, "a-first"), "first", 0o644)

	if err := os.Symlink("a-first", filepath.Join(root, "link")); err != nil {
		t.Fatal(err)
	}

	t.Setenv("PATH", t.TempDir())

	first := filepath.Join(archiveDir, "first.tar.zst")
	second := filepath.Join(archiveDir, "second.tar.zst")
	tarZstd(root, first)

	later := time.Unix(1_800_000_000, 0)

	if err := os.Chtimes(filepath.Join(root, "a-first"), later, later); err != nil {
		t.Fatal(err)
	}

	tarZstd(root, second)

	firstData, err := os.ReadFile(first)

	if err != nil {
		t.Fatal(err)
	}

	secondData, err := os.ReadFile(second)

	if err != nil {
		t.Fatal(err)
	}

	if !bytes.Equal(firstData, secondData) {
		t.Fatal("archive depends on filesystem timestamps")
	}

	entries := readTestArchive(t, firstData)
	want := []testArchiveEntry{
		{name: "a-first", mode: 0o644, body: "first"},
		{name: "bin", mode: 0o755},
		{name: "bin/tool", mode: 0o755, body: "tool"},
		{name: "link", mode: 0o777, link: "a-first"},
		{name: "z-last", mode: 0o644, body: "last"},
	}

	if !reflect.DeepEqual(entries, want) {
		t.Fatalf("archive entries:\n got: %#v\nwant: %#v", entries, want)
	}
}

type testArchiveEntry struct {
	name string
	mode int64
	body string
	link string
}

func writeArchiveTestFile(t *testing.T, path, body string, mode os.FileMode) {
	t.Helper()

	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}

	if err := os.WriteFile(path, []byte(body), mode); err != nil {
		t.Fatal(err)
	}
}

func readTestArchive(t *testing.T, data []byte) []testArchiveEntry {
	t.Helper()

	decoder, err := zstd.NewReader(bytes.NewReader(data))

	if err != nil {
		t.Fatal(err)
	}

	defer decoder.Close()

	reader := tar.NewReader(decoder)
	var entries []testArchiveEntry

	for {
		header, err := reader.Next()

		if err == io.EOF {
			break
		}

		if err != nil {
			t.Fatal(err)
		}

		if header.Uid != 0 || header.Gid != 0 || !header.ModTime.Equal(time.Unix(0, 0)) {
			t.Fatalf("non-reproducible metadata in %q: %#v", header.Name, header)
		}

		body, err := io.ReadAll(reader)

		if err != nil {
			t.Fatal(err)
		}

		entries = append(entries, testArchiveEntry{
			name: header.Name,
			mode: header.Mode,
			body: string(body),
			link: header.Linkname,
		})
	}

	return entries
}

// tracing-tree's ui tests build `test_dependencies/Cargo.toml`, a package with
// a lockfile of its own; `cargo vendor --sync` vendors both lockfiles' packages.
func TestVendorSyncTakesTheUnionOfTheLockfiles(t *testing.T) {
	registry := "registry+https://github.com/rust-lang/crates.io-index"
	log := Pkg{name: "log", version: "0.4.22", source: registry, checksum: "a"}
	futures := Pkg{name: "futures", version: "0.3.31", source: registry, checksum: "b"}

	got := mergeLockPackages([]Pkg{log}, []Pkg{log, futures})

	if len(got) != 2 || got[0] != log || got[1] != futures {
		t.Fatalf("merged = %v", got)
	}
}

// pin-project-lite's dev-dependencies come from git; cargo vendor copies such a
// package into the vendor directory under its name, as a registry one.
func TestAGitPackageIsVendoredUnderItsName(t *testing.T) {
	pkgs := []Pkg{
		{name: "macrotest", version: "1.2.1", source: "git+https://github.com/taiki-e/macrotest.git?branch=dev-old-msrv#07ad470b0c8aa315c808adb692ed8292b5bf89e8"},
		{name: "prettyplease", version: "0.1.25", source: "git+https://github.com/taiki-e/prettyplease.git?branch=dev-old-msrv#cd08a29d5e6b6c10784c08caca2361a31f992a3a"},
		{name: "prettyplease", version: "0.2.37", source: "registry+https://github.com/rust-lang/crates.io-index", checksum: "c"},
		{name: "pin-project-lite", version: "0.2.16"},
	}

	layout := vendorLayout(pkgs, false)

	if layout[0] != "macrotest" || layout[1] != "prettyplease-0.1.25" || layout[2] != "prettyplease-0.2.37" {
		t.Fatalf("layout = %v", layout)
	}
	if _, ok := layout[3]; ok {
		t.Fatal("a workspace package is vendored")
	}
}

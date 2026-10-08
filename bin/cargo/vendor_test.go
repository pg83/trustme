package main

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"reflect"
	"strings"
	"sync/atomic"
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

// pulldown-cmark's fuzz member takes mozjs from git, and mozjs inherits
// `edition`, `license`, `libc` and `cc` from its repository's `[workspace]`.
// Cargo vendors a git package with the manifest it normalized, so the copy
// loads with no workspace above it.
func TestAVendoredGitPackageCarriesWhatItInherits(t *testing.T) {
	root := t.TempDir()
	files := map[string]string{
		"repo/Cargo.toml": `[workspace]
members = ["mozjs"]

[workspace.package]
edition = "2021"
license = "MPL-2.0"

[workspace.dependencies]
libc = "0.2"
cc = { version = "1.0", features = ["parallel"] }
`,
		"repo/mozjs/Cargo.toml": `[package]
name = "mozjs"
version = "0.14.1"
edition.workspace = true
license.workspace = true

[dependencies]
libc.workspace = true

[build-dependencies]
cc = { workspace = true, features = ["jobserver"] }
`,
		"repo/mozjs/src/lib.rs": "",
		"project/Cargo.toml": `[package]
name = "demo"
version = "0.1.0"
`,
	}

	for name, text := range files {
		path := filepath.Join(root, filepath.FromSlash(name))

		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte(text), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	vendorDir := filepath.Join(root, "vendor")
	vendorGitPackage(filepath.Join(root, "repo", "mozjs"), filepath.Join(vendorDir, "mozjs"))

	repository := newRepository(findWorkspace(filepath.Join(root, "project", "Cargo.toml")), vendorDir)
	mozjs := repository.byName["mozjs"]

	if len(mozjs) != 1 || mozjs[0].edition != "2021" {
		t.Fatalf("vendored mozjs = %+v", mozjs)
	}
	if main := mozjs[0].dependencies.main; len(main) != 1 || main[0].name != "libc" || !main[0].version.accepts(parseVersion("0.2.170")) {
		t.Fatalf("mozjs dependencies = %+v", main)
	}

	build := mozjs[0].dependencies.build
	if len(build) != 1 || build[0].name != "cc" || strings.Join(build[0].features, ",") != "parallel,jobserver" {
		t.Fatalf("mozjs build-dependencies = %+v", build)
	}
	if _, err := os.Stat(filepath.Join(vendorDir, "mozjs", "src", "lib.rs")); err != nil {
		t.Fatal(err)
	}
}

// icu4x takes diplomat from git, and diplomat's `macro/Cargo.toml` inherits
// `version` from the repository's `[workspace.package]`: the package is found
// by the version its manifest resolves to.
func TestAGitPackageIsFoundByItsInheritedVersion(t *testing.T) {
	root := t.TempDir()

	for name, text := range map[string]string{
		"Cargo.toml":       "[workspace]\nmembers = [\"macro\", \"core\"]\n\n[workspace.package]\nversion = \"0.15.0\"\n",
		"core/Cargo.toml":  "[package]\nname = \"diplomat_core\"\nversion.workspace = true\n",
		"macro/Cargo.toml": "[package]\nname = \"diplomat\"\nversion.workspace = true\n",
	} {
		path := filepath.Join(root, filepath.FromSlash(name))

		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte(text), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	if dir := gitPackageDir(root, "diplomat", "0.15.0"); dir != filepath.Join(root, "macro") {
		t.Fatalf("diplomat found at %q", dir)
	}
	if dir := gitPackageDir(root, "diplomat", "0.14.0"); dir != "" {
		t.Fatalf("diplomat 0.14.0 found at %q", dir)
	}
}

// zstd-safe's zstd-sys builds the C library from its `zstd` submodule. Cargo
// checks out a git source's submodules, recursively, at the commits the
// superproject records (`update_submodules`, sources/git/utils.rs), so the
// vendored package carries their files; it lists no `.git` entry.
func TestAVendoredGitPackageCarriesItsSubmodules(t *testing.T) {
	root := t.TempDir()
	t.Setenv("GIT_CONFIG_COUNT", "1")
	t.Setenv("GIT_CONFIG_KEY_0", "protocol.file.allow")
	t.Setenv("GIT_CONFIG_VALUE_0", "always")

	git := func(dir string, args ...string) string {
		command := exec.Command("git", append([]string{"-C", dir, "-c", "user.name=t", "-c", "user.email=t@t"}, args...)...)
		out, err := command.CombinedOutput()

		if err != nil {
			t.Fatalf("git %v: %v\n%s", args, err, out)
		}

		return strings.TrimSpace(string(out))
	}
	repo := func(name string, files map[string]string) string {
		dir := filepath.Join(root, name)

		for file, text := range files {
			path := filepath.Join(dir, filepath.FromSlash(file))

			if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
				t.Fatal(err)
			}
			if err := os.WriteFile(path, []byte(text), 0o644); err != nil {
				t.Fatal(err)
			}
		}

		git(dir, "init", "-q", "-b", "main")
		git(dir, "config", "uploadpack.allowAnySHA1InWant", "true")
		git(dir, "add", ".")
		git(dir, "commit", "-q", "-m", "one")

		return dir
	}

	zstd := repo("zstd", map[string]string{"lib/zstd.h": "pinned\n"})
	top := repo("zstd-rs", map[string]string{
		"Cargo.toml":          "[workspace]\nmembers = [\"zstd-sys\"]\n",
		"zstd-sys/Cargo.toml": "[package]\nname = \"zstd-sys\"\nversion = \"2.0.0\"\n",
		"zstd-sys/src/lib.rs": "",
	})
	git(top, "submodule", "add", "-q", "file://"+zstd, "zstd-sys/zstd")
	git(top, "commit", "-q", "-m", "zstd")
	commit := git(top, "rev-parse", "HEAD")
	throw(os.WriteFile(filepath.Join(zstd, "lib", "zstd.h"), []byte("after\n"), 0o644))
	git(zstd, "commit", "-q", "-am", "after")

	dest := filepath.Join(root, "vendor", "zstd-sys")
	fetchGitPackage(Pkg{name: "zstd-sys", version: "2.0.0", source: "git+file://" + top + "#" + commit}, dest)

	data, err := os.ReadFile(filepath.Join(dest, "zstd", "lib", "zstd.h"))

	if err != nil || string(data) != "pinned\n" {
		t.Fatalf("vendored zstd.h = %q, %v", data, err)
	}
	if _, err := os.Lstat(filepath.Join(dest, "zstd", ".git")); err == nil {
		t.Fatal("the vendored submodule carries its .git")
	}
}

// Cargo downloads the packages it vendors together: `PackageSet::get_many`
// starts every download, then waits for them (curl multi over HTTP/2). The
// server here answers only once all of them are in flight.
func TestVendoringDownloadsTheCratesTogether(t *testing.T) {
	const count = 2
	registry := "registry+https://github.com/rust-lang/crates.io-index"
	crates := map[string][]byte{}
	var pkgs []Pkg

	for i := 0; i < count; i++ {
		name := fmt.Sprintf("c%d", i)
		var archive bytes.Buffer
		gz := gzip.NewWriter(&archive)
		tw := tar.NewWriter(gz)
		manifest := fmt.Sprintf("[package]\nname = %q\nversion = \"1.0.0\"\n", name)

		throw(tw.WriteHeader(&tar.Header{Name: name + "-1.0.0/Cargo.toml", Mode: 0o644, Size: int64(len(manifest)), Typeflag: tar.TypeReg}))
		throw2(tw.Write([]byte(manifest)))
		throw(tw.Close())
		throw(gz.Close())

		sum := sha256.Sum256(archive.Bytes())
		crates["/crates/"+name+"/"+name+"-1.0.0.crate"] = archive.Bytes()
		pkgs = append(pkgs, Pkg{name: name, version: "1.0.0", source: registry, checksum: hex.EncodeToString(sum[:])})
	}

	var arrived atomic.Int32
	all := make(chan struct{})
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if arrived.Add(1) == count {
			close(all)
		}

		select {
		case <-all:
			throw2(w.Write(crates[r.URL.Path]))
		case <-time.After(5 * time.Second):
			http.Error(w, "the downloads came one at a time", http.StatusServiceUnavailable)
		}
	}))
	defer server.Close()

	saved := crateHost
	crateHost = server.URL
	defer func() {
		crateHost = saved
	}()

	vendorDir := t.TempDir()

	if e := try(func() { vendorAll(pkgs, vendorDir, false) }); e != nil {
		t.Fatal(e.error())
	}

	for _, p := range pkgs {
		if _, err := os.Stat(filepath.Join(vendorDir, p.name, "Cargo.toml")); err != nil {
			t.Fatal(err)
		}
	}
}

func TestVendoringKeepsTwoConnectionsToTheRegistry(t *testing.T) {
	const count = 8
	registry := "registry+https://github.com/rust-lang/crates.io-index"
	crates := map[string][]byte{}
	var pkgs []Pkg

	for i := 0; i < count; i++ {
		name := fmt.Sprintf("k%d", i)
		var archive bytes.Buffer
		gz := gzip.NewWriter(&archive)
		tw := tar.NewWriter(gz)
		manifest := fmt.Sprintf("[package]\nname = %q\nversion = \"1.0.0\"\n", name)

		throw(tw.WriteHeader(&tar.Header{Name: name + "-1.0.0/Cargo.toml", Mode: 0o644, Size: int64(len(manifest)), Typeflag: tar.TypeReg}))
		throw2(tw.Write([]byte(manifest)))
		throw(tw.Close())
		throw(gz.Close())

		sum := sha256.Sum256(archive.Bytes())
		crates["/crates/"+name+"/"+name+"-1.0.0.crate"] = archive.Bytes()
		pkgs = append(pkgs, Pkg{name: name, version: "1.0.0", source: registry, checksum: hex.EncodeToString(sum[:])})
	}

	var open, most atomic.Int32
	server := httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		time.Sleep(20 * time.Millisecond)
		throw2(w.Write(crates[r.URL.Path]))
	}))
	server.Config.ConnState = func(conn net.Conn, state http.ConnState) {
		switch state {
		case http.StateNew:
			now := open.Add(1)
			for {
				seen := most.Load()
				if now <= seen || most.CompareAndSwap(seen, now) {
					break
				}
			}
		case http.StateClosed, http.StateHijacked:
			open.Add(-1)
		}
	}
	server.Start()
	defer server.Close()

	saved := crateHost
	crateHost = server.URL
	defer func() {
		crateHost = saved
	}()

	vendorDir := t.TempDir()

	if e := try(func() { vendorAll(pkgs, vendorDir, false) }); e != nil {
		t.Fatal(e.error())
	}

	if most.Load() > 2 {
		t.Fatalf("%d connections to the registry at once", most.Load())
	}
}

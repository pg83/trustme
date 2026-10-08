package main

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/BurntSushi/toml"
	"github.com/klauspost/compress/zstd"
)

var crateHost = "https://static.crates.io"

func crateURL(p Pkg) string {
	return fmt.Sprintf("%s/crates/%s/%s-%s.crate", crateHost, p.name, p.name, p.version)
}

func vendorLayout(pkgs []Pkg, versioned bool) map[int]string {
	counts := map[string]int{}

	for _, p := range pkgs {
		if p.isRegistry() || p.isGit() {
			counts[p.name]++
		}
	}

	names := map[int]string{}

	for i, p := range pkgs {
		if !p.isRegistry() && !p.isGit() {
			continue
		}

		if versioned || counts[p.name] > 1 {
			names[i] = fmt.Sprintf("%s-%s", p.name, p.version)
		} else {
			names[i] = p.name
		}
	}

	return names
}

// Cargo starts the download of every package it vendors, then waits for them
// all (`PackageSet::get_many`, curl multi over HTTP/2). A failure is reported
// for the first package in the lockfile's order that failed.
func vendorAll(pkgs []Pkg, vendorDir string, versioned bool) {
	layout := vendorLayout(pkgs, versioned)
	transport := http.DefaultTransport.(*http.Transport).Clone()
	transport.MaxConnsPerHost = 2
	client := &http.Client{Timeout: 120 * time.Second, Transport: transport}
	failures := make([]*Exception, len(pkgs))
	total := len(layout)
	var started atomic.Int32
	var done sync.WaitGroup

	for i, p := range pkgs {
		dir, ok := layout[i]

		if !ok {
			continue
		}

		done.Add(1)

		go func() {
			defer done.Done()

			fmt.Fprintf(os.Stderr, "vendoring (%d/%d) %s %s\n", started.Add(1), total, p.name, p.version)

			failures[i] = try(func() {
				dest := filepath.Join(vendorDir, dir)

				if p.isGit() {
					fetchGitPackage(p, dest)
				} else {
					fetchCrate(client, p, dest)
				}
			})
		}()
	}

	done.Wait()

	for i, failure := range failures {
		if failure != nil {
			throwFmt("%s %s: %v", pkgs[i].name, pkgs[i].version, failure.error())
		}
	}
}

func fetchCrate(client *http.Client, p Pkg, dest string) {
	resp := throw2(client.Get(crateURL(p)))

	defer func() {
		throw(resp.Body.Close())
	}()

	if resp.StatusCode != http.StatusOK {
		throwFmt("GET %s: %s", crateURL(p), resp.Status)
	}

	data := throw2(io.ReadAll(resp.Body))
	sum := sha256.Sum256(data)
	got := hex.EncodeToString(sum[:])

	if got != p.checksum {
		throwFmt("checksum mismatch: got %s want %s", got, p.checksum)
	}

	throw(os.RemoveAll(dest))
	throw(os.MkdirAll(dest, 0o755))
	extractCrate(data, dest)

	meta := map[string]any{"files": map[string]string{}, "package": p.checksum}
	buf := throw2(json.Marshal(meta))

	throw(os.WriteFile(filepath.Join(dest, ".cargo-checksum.json"), buf, 0o644))
}

// A git source is `git+<url>?<reference>#<commit>`; cargo vendor checks out the
// locked commit and copies the package of that name out of the repository, with
// a checksum file whose `package` is null (`cargo_util::vendor`, git sources).
// The checkout brings its submodules, recursively, at the commits it records,
// a relative submodule url taken against the repository's
// (`update_submodules`, sources/git/utils.rs).
func fetchGitPackage(p Pkg, dest string) {
	url := strings.TrimPrefix(p.source, "git+")
	commit := ""

	if i := strings.IndexByte(url, '#'); i >= 0 {
		url, commit = url[:i], url[i+1:]
	}

	if i := strings.IndexByte(url, '?'); i >= 0 {
		url = url[:i]
	}

	if commit == "" {
		throwFmt("git source %s has no locked commit", p.source)
	}

	checkout := throw2(os.MkdirTemp("", "cargo-vendor-git-"))

	defer func() {
		throw(os.RemoveAll(checkout))
	}()

	for _, args := range [][]string{
		{"init", "-q", checkout},
		{"-C", checkout, "remote", "add", "origin", url},
		{"-C", checkout, "fetch", "-q", "--depth", "1", "origin", commit},
		{"-C", checkout, "checkout", "-q", "FETCH_HEAD"},
		{"-C", checkout, "submodule", "update", "-q", "--init", "--recursive"},
	} {
		command := exec.Command("git", append([]string{"-c", "gc.auto=0", "-c", "maintenance.auto=false"}, args...)...)
		command.Stderr = os.Stderr
		throw(command.Run())
	}

	packageDir := gitPackageDir(checkout, p.name, p.version)

	if packageDir == "" {
		throwFmt("no package %s %s in %s at %s", p.name, p.version, url, commit)
	}

	vendorGitPackage(packageDir, dest)
}

// Cargo finds a git dependency among the packages of the checkout by the
// name and version their manifests resolve to (`GitSource` reads every
// package of the repository), a version inherited from `[workspace.package]`
// included.
func gitPackageDir(checkout, name, version string) string {
	var packageDir string

	throw(filepath.WalkDir(checkout, func(path string, entry fs.DirEntry, err error) error {
		throw(err)

		if entry.IsDir() && (entry.Name() == ".git" || entry.Name() == "target") {
			return filepath.SkipDir
		}

		if packageDir == "" && entry.Name() == "Cargo.toml" {
			table := mapValue(readToml(path)["package"])

			if stringValue(table["name"]) == name && stringValue(mapValue(normalizedManifest(path)["package"])["version"]) == version {
				packageDir = filepath.Dir(path)
			}
		}

		return nil
	}))

	return packageDir
}

// Cargo vendors a git package as its files and the manifest it normalized
// (`prepare_for_vendor`, cargo/ops/vendor.rs): what the package inherits from
// the `[workspace]` of its repository is written out, since no workspace
// stands above the vendored copy.
func vendorGitPackage(packageDir, dest string) {
	throw(os.RemoveAll(dest))
	copyPackageTree(packageDir, dest)

	var manifest bytes.Buffer

	throw(toml.NewEncoder(&manifest).Encode(normalizedManifest(filepath.Join(packageDir, "Cargo.toml"))))
	throw(os.WriteFile(filepath.Join(dest, "Cargo.toml"), manifest.Bytes(), 0o644))

	buf := throw2(json.Marshal(map[string]any{"files": map[string]string{}, "package": nil}))

	throw(os.WriteFile(filepath.Join(dest, ".cargo-checksum.json"), buf, 0o644))
}

// A field or dependency marked `workspace = true` takes the workspace's
// value; a dependency adds its own features, `optional` and `public` to the
// workspace's declaration and may turn back on the default features the
// workspace turned off (`inherit_workspace_dep`, cargo/util/toml/mod.rs).
func normalizedManifest(manifestPath string) map[string]any {
	doc := readToml(manifestPath)
	workspace := findWorkspace(manifestPath)
	table := mapValue(readToml(workspace.manifestPath)["workspace"])
	inheritedPackage := mapValue(table["package"])
	inheritedDependencies := mapValue(table["dependencies"])
	packageDir := filepath.Dir(manifestPath)

	for key, value := range mapValue(doc["package"]) {
		if !boolValue(mapValue(value)["workspace"], false) {
			continue
		}

		inherited, ok := inheritedPackage[key]

		if !ok {
			throwFmt("%s: `package.%s` is not defined by the workspace", manifestPath, key)
		}

		if path, isPath := inherited.(string); isPath && (key == "readme" || key == "license-file") {
			inherited = throw2(filepath.Rel(packageDir, filepath.Join(workspace.dir, path)))
		}

		mapValue(doc["package"])[key] = inherited
	}

	normalize := func(dependencies map[string]any) {
		for key, value := range dependencies {
			member := mapValue(value)

			if !boolValue(member["workspace"], false) {
				continue
			}

			declared, ok := inheritedDependencies[key]

			if !ok {
				throwFmt("workspace dependency %q is not defined", key)
			}

			resolved := map[string]any{}

			if version, isVersion := declared.(string); isVersion {
				resolved["version"] = version
			} else {
				for field, fieldValue := range mapValue(declared) {
					resolved[field] = fieldValue
				}
			}

			if path := stringValue(resolved["path"]); path != "" {
				resolved["path"] = throw2(filepath.Rel(packageDir, filepath.Join(workspace.dir, path)))
			}

			if features := append(stringsValue(resolved["features"]), stringsValue(member["features"])...); len(features) > 0 {
				resolved["features"] = features
			}

			for _, field := range []string{"optional", "public"} {
				if fieldValue, present := member[field]; present {
					resolved[field] = fieldValue
				}
			}

			for _, field := range []string{"default-features", "default_features"} {
				if boolValue(member[field], false) {
					delete(resolved, "default_features")
					resolved["default-features"] = true
				}
			}

			dependencies[key] = resolved
		}
	}

	sections := []string{"dependencies", "dev-dependencies", "dev_dependencies", "build-dependencies", "build_dependencies"}

	for _, section := range sections {
		normalize(mapValue(doc[section]))
	}

	for _, target := range mapValue(doc["target"]) {
		for _, section := range sections {
			normalize(mapValue(mapValue(target)[section]))
		}
	}

	return doc
}

func copyPackageTree(from, to string) {
	throw(filepath.WalkDir(from, func(path string, entry fs.DirEntry, err error) error {
		throw(err)

		rel := throw2(filepath.Rel(from, path))
		target := filepath.Join(to, rel)

		if rel != "." && entry.Name() == ".git" {
			if entry.IsDir() {
				return filepath.SkipDir
			}

			return nil
		}

		if entry.IsDir() {
			if rel != "." && entry.Name() == "target" {
				return filepath.SkipDir
			}

			throw(os.MkdirAll(target, 0o755))

			return nil
		}

		if !entry.Type().IsRegular() {
			return nil
		}

		info := throw2(entry.Info())
		data := throw2(os.ReadFile(path))

		throw(os.WriteFile(target, data, info.Mode().Perm()))

		return nil
	}))
}

func extractCrate(data []byte, dest string) {
	gz := throw2(gzip.NewReader(bytes.NewReader(data)))

	defer func() {
		throw(gz.Close())
	}()

	tr := tar.NewReader(gz)

	for {
		hdr, err := tr.Next()

		if err == io.EOF {
			break
		}

		throw(err)

		rel := hdr.Name

		if i := strings.IndexByte(rel, '/'); i >= 0 {
			rel = rel[i+1:]
		} else {
			continue
		}

		if rel == "" {
			continue
		}

		target := filepath.Join(dest, filepath.Clean("/"+rel))

		switch hdr.Typeflag {
		case tar.TypeDir:
			throw(os.MkdirAll(target, 0o755))
		case tar.TypeReg:
			extractRegularFile(tr, target, os.FileMode(hdr.Mode)&0o777)
		}
	}
}

func extractRegularFile(tr *tar.Reader, target string, mode os.FileMode) {
	throw(os.MkdirAll(filepath.Dir(target), 0o755))

	f := throw2(os.OpenFile(target, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, mode))

	defer func() {
		throw(f.Close())
	}()

	throw2(io.Copy(f, tr))
}

func writeConfig(root string) {
	dir := filepath.Join(root, ".cargo")

	throw(os.MkdirAll(dir, 0o755))

	const cfg = `[source.crates-io]
replace-with = "vendored-sources"

[source.vendored-sources]
directory = "vendor"
`
	throw(os.WriteFile(filepath.Join(dir, "config.toml"), []byte(cfg), 0o644))
}

func tarZstd(root, out string) {
	dest := throw2(os.Create(out))

	defer func() {
		throw(dest.Close())
	}()

	encoder := throw2(zstd.NewWriter(dest, zstd.WithEncoderConcurrency(1)))

	defer func() {
		throw(encoder.Close())
	}()

	archive := tar.NewWriter(encoder)

	defer func() {
		throw(archive.Close())
	}()

	throw(filepath.Walk(root, func(path string, info os.FileInfo, walkErr error) error {
		return try(func() {
			throw(walkErr)

			rel := throw2(filepath.Rel(root, path))

			if rel == "." {
				return
			}

			link := ""

			if info.Mode()&os.ModeSymlink != 0 {
				link = throw2(os.Readlink(path))
			}

			header := throw2(tar.FileInfoHeader(info, link))
			header.Name = filepath.ToSlash(rel)
			header.Uid = 0
			header.Gid = 0
			header.Uname = ""
			header.Gname = ""
			header.ModTime = time.Unix(0, 0).UTC()
			header.AccessTime = time.Time{}
			header.ChangeTime = time.Time{}

			throw(archive.WriteHeader(header))

			if info.Mode().IsRegular() {
				src := throw2(os.Open(path))

				defer func() {
					throw(src.Close())
				}()

				throw2(io.Copy(archive, src))
			}
		}).asError()
	}))
}

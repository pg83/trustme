package main

import (
	"path/filepath"
	"testing"
)

// tokio declares `libc` optional under `cfg(unix)`, under
// `cfg(all(tokio_unstable, target_os = "linux"))` and under `cfg(target_os =
// "wasi")`. Cargo's `dep:libc` enables the optional dependency of that name in
// every table that declares it; enabling the first declaration a map happened
// to yield left the `cfg(unix)` one off in some runs, and tokio lost its
// `--extern libc`.
func TestFeatureEnablesEveryDeclarationOfItsDependency(t *testing.T) {
	dir := t.TempDir()

	writeTestFile(t, filepath.Join(dir, "src", "lib.rs"), "")

	manifest := `[package]
name = "demo"
version = "1.0.0"
edition = "2021"

[features]
net = ["dep:libc"]

[target.'cfg(all(tokio_unstable, target_os = "linux"))'.dependencies]
libc = { version = "0.2", optional = true }

[target.'cfg(unix)'.dependencies]
libc = { version = "0.2", optional = true }

[target.'cfg(target_os = "wasi")'.dependencies]
libc = { version = "0.2", optional = true }
`

	path := filepath.Join(dir, "Cargo.toml")

	writeTestFile(t, path, manifest)

	for run := 0; run < 20; run++ {
		workspace := &Workspace{dir: dir, dependencies: map[string]*Dependency{}, patches: map[string]string{}}
		pkg := parsePackage(path, readToml(path), workspace)

		requestFeatures(pkg, []string{"net"}, false)
		expandFeatures(pkg)

		for condition, group := range pkg.targetDeps {
			for _, dep := range group.main {
				if dep.key == "libc" && !dep.enabled {
					t.Fatalf("run %d: libc under %s was not enabled", run, condition)
				}
			}
		}
	}
}

// num enables `num-rational/num-bigint-std`, which num-rational declares as
// `["num-bigint/std"]`, and gates `BigRational` on `feature = "num-bigint"`.
// Cargo's `dep/feature` enables the optional dependency and, to keep the old
// behaviour, the package's feature of the same name when there is one
// (`activate_dep_feature`); `dep?/feature` enables neither.
func TestADependencyFeatureEnablesTheFeatureNamedAfterTheDependency(t *testing.T) {
	dir := t.TempDir()

	writeTestFile(t, filepath.Join(dir, "src", "lib.rs"), "")

	manifest := `[package]
name = "demo"
version = "1.0.0"
edition = "2021"

[features]
num-bigint = ["dep:num-bigint"]
num-bigint-std = ["num-bigint/std"]
weak-std = ["num-bigint?/std"]

[dependencies]
num-bigint = { version = "0.4", optional = true }
`

	path := filepath.Join(dir, "Cargo.toml")

	writeTestFile(t, path, manifest)

	for _, entry := range []struct {
		request string
		want    bool
	}{
		{"num-bigint-std", true},
		{"weak-std", false},
	} {
		workspace := &Workspace{dir: dir, dependencies: map[string]*Dependency{}, patches: map[string]string{}}
		pkg := parsePackage(path, readToml(path), workspace)

		requestFeatures(pkg, []string{entry.request}, false)
		expandFeatures(pkg)

		if pkg.activeFeatures["num-bigint"] != entry.want {
			t.Fatalf("%s: feature num-bigint active = %v, want %v", entry.request, pkg.activeFeatures["num-bigint"], entry.want)
		}
	}
}

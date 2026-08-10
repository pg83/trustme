package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestManifestFeaturesAndTargets(t *testing.T) {
	dir := t.TempDir()

	writeTestFile(t, filepath.Join(dir, "src", "lib.rs"), "")
	writeTestFile(t, filepath.Join(dir, "src", "main.rs"), "")

	manifest := `[package]
name = "demo"
version = "1.2.3"
edition = "2024"

[dependencies]
serde = { version = "1", optional = true }
renamed = { package = "real-name", version = "2", default-features = false, features = ["x"] }

[target.'cfg(unix)'.dependencies]
unix-only = "3"

[features]
default = ["serde", "renamed/extra"]
weak = ["serde?/derive"]

[[bin]]
name = "tool"
required-features = ["serde"]
`

	path := filepath.Join(dir, "Cargo.toml")

	writeTestFile(t, path, manifest)

	workspace := &Workspace{dir: dir, dependencies: map[string]*Dependency{}, patches: map[string]string{}}
	pkg := parsePackage(path, readToml(path), workspace)

	if pkg.name != "demo" || pkg.edition != "2024" {
		t.Fatalf("unexpected package: %#v", pkg)
	}

	if packageLibrary(pkg) == nil {
		t.Fatal("implicit library target was not created")
	}

	if len(pkg.targets) != 3 {
		t.Fatalf("got %d targets, want lib, explicit bin, implicit bin", len(pkg.targets))
	}

	requestFeatures(pkg, pkg.defaultFeature, false)
	expandFeatures(pkg)

	if !findDependency(pkg, "serde").enabled {
		t.Fatal("optional dependency was not enabled")
	}

	if !contains(findDependency(pkg, "renamed").features, "extra") {
		t.Fatal("dependency feature was not propagated")
	}
}

func TestWorkspaceMembers(t *testing.T) {
	dir := t.TempDir()
	writeTestFile(t, filepath.Join(dir, "Cargo.toml"), `[workspace]
members = ["crates/*"]
exclude = ["crates/skip"]
`)
	writeTestFile(t, filepath.Join(dir, "crates", "one", "Cargo.toml"), `[package]
name = "one"
version = "1.0.0"
`)
	writeTestFile(t, filepath.Join(dir, "crates", "skip", "Cargo.toml"), `[package]
name = "skip"
version = "1.0.0"
`)
	workspace := loadWorkspace(filepath.Join(dir, "Cargo.toml"))
	members := workspaceMemberManifests(workspace)

	if len(members) != 1 || selectWorkspacePackage(workspace, members, "one") != members[0] {
		t.Fatalf("unexpected workspace members: %#v", members)
	}
}

func TestPathPackagePrecedesRegistryLock(t *testing.T) {
	dir := t.TempDir()
	pathPackage := &Package{name: "shim", version: parseVersion("1.99.0"), dir: filepath.Join(dir, "workspace", "shim")}
	vendorPackage := &Package{name: "shim", version: parseVersion("1.0.0"), dir: filepath.Join(dir, "vendor", "shim")}
	repository := &Repository{
		vendorDir: filepath.Join(dir, "vendor"),
		byName:    map[string][]*Package{"shim": {vendorPackage, pathPackage}},
		locked:    map[string]map[Version]bool{"shim": {parseVersion("1.0.0"): true}},
	}
	dep := &Dependency{key: "shim", name: "shim"}

	if got := repository.resolve(dep, &Package{name: "root"}); got != pathPackage {
		t.Fatalf("resolved %#v, want path package", got)
	}
}

func TestRepositoryLoadsSymlinkedVendorPackage(t *testing.T) {
	dir := t.TempDir()
	workspaceManifest := filepath.Join(dir, "Cargo.toml")
	vendorDir := filepath.Join(dir, "vendor")
	packageDir := filepath.Join(dir, "packages", "linked-1.2.3")

	writeTestFile(t, workspaceManifest, `[workspace]
`)
	writeTestFile(t, filepath.Join(packageDir, "Cargo.toml"), `[package]
name = "linked"
version = "1.2.3"
`)

	if err := os.MkdirAll(vendorDir, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(packageDir, filepath.Join(vendorDir, "linked-1.2.3")); err != nil {
		t.Fatal(err)
	}

	repository := newRepository(loadWorkspace(workspaceManifest), vendorDir)
	packages := repository.byName["linked"]

	if len(packages) != 1 || packages[0].version != parseVersion("1.2.3") {
		t.Fatalf("symlinked vendor package was not loaded: %#v", packages)
	}
}

func writeTestFile(t *testing.T, path, contents string) {
	t.Helper()

	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}

	if err := os.WriteFile(path, []byte(contents), 0o644); err != nil {
		t.Fatal(err)
	}
}

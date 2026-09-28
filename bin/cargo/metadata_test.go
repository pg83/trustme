package main

import (
	"encoding/json"
	"os"
	"strings"
	"path/filepath"
	"testing"
)

func TestMetadataNoDepsWorkspace(t *testing.T) {
	dir := t.TempDir()
	member := filepath.Join(dir, "macro")
	if err := os.MkdirAll(filepath.Join(member, "src"), 0o755); err != nil {
		t.Fatal(err)
	}
	rootManifest := filepath.Join(dir, "Cargo.toml")
	if err := os.WriteFile(rootManifest, []byte("[package]\nname = \"root\"\nversion = \"1.2.3\"\nedition = \"2021\"\n\n[workspace]\nmembers = [\"macro\"]\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "src.rs"), nil, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(member, "Cargo.toml"), []byte("[package]\nname = \"macro\"\nversion = \"0.1.0\"\nedition = \"2024\"\n\n[lib]\nproc-macro = true\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(member, "src", "lib.rs"), nil, 0o644); err != nil {
		t.Fatal(err)
	}

	output := buildMetadata(MetadataOptions{manifestPath: rootManifest, targetDir: filepath.Join(dir, "out"), format: "1", noDeps: true})
	if len(output.Packages) != 2 || len(output.WorkspaceMembers) != 2 || len(output.WorkspaceDefaultMembers) != 1 {
		t.Fatalf("unexpected workspace metadata: %#v", output)
	}
	var macro *MetadataPackage
	for i := range output.Packages {
		if output.Packages[i].Name == "macro" {
			macro = &output.Packages[i]
		}
	}
	if macro == nil || len(macro.Targets) != 1 || macro.Targets[0].CrateTypes[0] != "proc-macro" {
		t.Fatalf("unexpected proc macro package: %#v", macro)
	}
	if output.TargetDirectory != filepath.Join(dir, "out") || output.WorkspaceRoot != dir {
		t.Fatalf("unexpected paths: %#v", output)
	}
}

// ui_test reads `cargo metadata` with the cargo_metadata crate, whose lists
// are sequences: cargo writes an empty list as `[]`, and leaves out
// `required-features` a target does not declare.
func TestMetadataListsAreNeverNull(t *testing.T) {
	dir := t.TempDir()
	manifest := filepath.Join(dir, "Cargo.toml")
	if err := os.MkdirAll(filepath.Join(dir, "src"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(manifest, []byte("[package]\nname = \"deps\"\nversion = \"0.1.0\"\nedition = \"2021\"\n\n[dependencies]\nglob = \"0.3\"\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "src", "lib.rs"), nil, 0o644); err != nil {
		t.Fatal(err)
	}

	encoded, err := json.Marshal(buildMetadata(MetadataOptions{manifestPath: manifest, format: "1", noDeps: true}))
	if err != nil {
		t.Fatal(err)
	}
	text := string(encoded)
	if strings.Contains(text, `"features":null`) || strings.Contains(text, `"authors":null`) || strings.Contains(text, `"keywords":null`) {
		t.Fatalf("a list is null: %s", text)
	}
	if strings.Contains(text, `"required-features"`) {
		t.Fatalf("an undeclared required-features is written: %s", text)
	}
}


// ui_test reads `cargo metadata` without `--no-deps` for the version of each
// dependency it built: the packages list names the resolved dependencies too.
func TestMetadataListsTheResolvedDependencies(t *testing.T) {
	dir := t.TempDir()
	for path, text := range map[string]string{
		"Cargo.toml":       "[package]\nname = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\nleaf = { path = \"leaf\" }\n\n[dev-dependencies]\ncheck = { path = \"check\" }\n",
		"leaf/Cargo.toml":  "[package]\nname = \"leaf\"\nversion = \"1.2.0\"\n",
		"check/Cargo.toml": "[package]\nname = \"check\"\nversion = \"0.3.0\"\n",
	} {
		full := filepath.Join(dir, path)
		if err := os.MkdirAll(filepath.Join(filepath.Dir(full), "src"), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(full, []byte(text), 0o644); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(filepath.Dir(full), "src", "lib.rs"), nil, 0o644); err != nil {
			t.Fatal(err)
		}
	}

	manifest := filepath.Join(dir, "Cargo.toml")
	workspace := findWorkspace(manifest)
	cfg := &CfgSet{flags: map[string]bool{"unix": true}, values: map[string]map[string]bool{}}
	names := map[string]string{}
	for _, pkg := range resolvedMetadataPackages(workspace, []string{manifest}, cfg) {
		names[pkg.name] = pkg.version.string()
	}
	if names["leaf"] != "1.2.0" || names["check"] != "0.3.0" || names["app"] != "0.1.0" {
		t.Fatalf("resolved packages = %v", names)
	}
}

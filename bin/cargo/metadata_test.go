package main

import (
	"os"
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

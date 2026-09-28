package main

import (
	"bytes"
	"os"
	"path/filepath"
	"testing"
)

func writeTreeManifest(t *testing.T, dir, text string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Join(dir, "src"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "Cargo.toml"), []byte(text), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "src", "lib.rs"), nil, 0o644); err != nil {
		t.Fatal(err)
	}
}

// rustix's backend test asks `cargo tree --edges=normal --invert=libc` whether
// a test crate links libc: the packages depending on it, printed as a tree, or
// nothing when the crate only declares it for another target or as an
// optional dependency no feature enables, and an error for a name the graph
// never mentions.
func TestTreeInvertPrintsTheDependents(t *testing.T) {
	dir := t.TempDir()
	writeTreeManifest(t, dir, "[package]\nname = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\nmid = { path = \"mid\" }\n")
	writeTreeManifest(t, filepath.Join(dir, "mid"), "[package]\nname = \"mid\"\nversion = \"0.2.0\"\n\n[dependencies]\nopt = { path = \"../opt\", optional = true }\n\n[target.'cfg(unix)'.dependencies]\nleaf = { path = \"../leaf\" }\n\n[target.'cfg(windows)'.dependencies]\nwin = { path = \"../win\" }\n")
	for _, name := range []string{"leaf", "opt", "win"} {
		writeTreeManifest(t, filepath.Join(dir, name), "[package]\nname = \""+name+"\"\nversion = \"1.0.0\"\n")
	}

	tree := func(invert string) string {
		manifest := filepath.Join(dir, "Cargo.toml")
		workspace := findWorkspace(manifest)
		repository := newRepository(workspace, "")
		root := repository.loadPath(manifest)
		context := &BuildContext{
			opts: BuildOptions{command: "build"}, repository: repository, root: root, workspace: workspace,
			host: "host", target: "host",
			cfg: &CfgSet{flags: map[string]bool{"unix": true}, values: map[string]map[string]bool{}},
		}
		resolveGraph(context)
		var out bytes.Buffer
		printTree(&out, context, root, []string{invert})

		return out.String()
	}

	want := "leaf v1.0.0 (" + filepath.Join(dir, "leaf") + ")\n└── mid v0.2.0 (" + filepath.Join(dir, "mid") + ")\n    └── app v0.1.0 (" + dir + ")\n"
	if got := tree("leaf"); got != want {
		t.Fatalf("inverted tree:\n%s\nwant:\n%s", got, want)
	}
	for _, absent := range []string{"opt", "win"} {
		if got := tree(absent); got != "" {
			t.Fatalf("%s is not linked, yet the tree is:\n%s", absent, got)
		}
	}
	if err := try(func() { tree("nosuch") }); err == nil {
		t.Fatal("a name nothing declares is not an error")
	}
}

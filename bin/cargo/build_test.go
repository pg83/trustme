package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestProcMacroTestDependsOnLinkedLibrary(t *testing.T) {
	root := t.TempDir()
	source := filepath.Join(root, "src", "lib.rs")

	if err := os.MkdirAll(filepath.Dir(source), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(source, nil, 0o644); err != nil {
		t.Fatal(err)
	}

	library := &Target{
		kind: "lib", name: "macro", path: "src/lib.rs", procMacro: true, test: true,
	}
	pkg := &Package{
		dir: root, manifestPath: filepath.Join(root, "Cargo.toml"), name: "macro",
		version: Version{major: 1}, targets: []*Target{library},
		activeFeatures: map[string]bool{},
	}
	context := &BuildContext{
		opts: BuildOptions{command: "test", profile: "debug", targetDir: filepath.Join(root, "target")},
		root: pkg, workspace: &Workspace{dir: root}, host: "host", target: "host",
	}
	builder := &Builder{context: context, tasks: map[string]*Task{}, units: map[*Task]*CompileUnit{}}
	roots, _ := builder.rootTasks()

	if len(roots) != 1 {
		t.Fatalf("root task count = %d, want one", len(roots))
	}

	testUnit := builder.units[roots[0]]
	libraryTask := builder.libraryTask(pkg, true)
	linkedLibrary := builder.finalTask(libraryTask)

	if testUnit == nil || testUnit.rs == nil {
		t.Fatal("test compile unit is missing")
	}
	if !containsTask(testUnit.rs.deps, linkedLibrary) {
		t.Fatal("proc-macro test does not depend on the linked library")
	}
	if containsTask(testUnit.rs.deps, libraryTask) {
		t.Fatal("proc-macro test depends on metadata without the linked library")
	}
}

func containsTask(tasks []*Task, want *Task) bool {
	for _, task := range tasks {
		if task == want {
			return true
		}
	}

	return false
}

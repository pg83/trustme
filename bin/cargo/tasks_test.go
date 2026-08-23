package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestCargoControlEnvironmentUsesTrustmePrefix(t *testing.T) {
	for _, name := range []string{
		trustmeCargoDumpCommand,
		trustmeCargoDumpEnv,
		trustmeCargoDylib,
		trustmeCargoIgnoreToolTimestamps,
		trustmeCargoNoDebugAssertions,
	} {
		if !strings.HasPrefix(name, "TRUSTME_CARGO_") {
			t.Errorf("Cargo control environment variable %q has the wrong namespace", name)
		}
	}
}

func TestCargoControlEnvironmentIsConsumed(t *testing.T) {
	t.Setenv(trustmeCargoDylib, "1")
	t.Setenv(trustmeCargoIgnoreToolTimestamps, "1")
	t.Setenv(trustmeCargoNoDebugAssertions, "1")

	if !dylibEnabled() {
		t.Error("TRUSTME_CARGO_DYLIB was ignored")
	}

	if !ignoreToolTimestamps() {
		t.Error("TRUSTME_CARGO_IGNORE_TOOL_TIMESTAMPS was ignored")
	}

	if debugAssertions("debug") {
		t.Error("TRUSTME_CARGO_NO_DEBUG_ASSERTIONS was ignored")
	}
}

func TestTaskExecutorCachesOutputsInCas(t *testing.T) {
	root := t.TempDir()
	input := filepath.Join(root, "input")

	if err := os.WriteFile(input, []byte("source"), 0o644); err != nil {
		t.Fatal(err)
	}

	runs := 0
	newTask := func() *Task {
		return &Task{
			key:     "test|compile",
			name:    "compile",
			kind:    "RS",
			inputs:  []string{input},
			outputs: []TaskOutput{{name: "artifact.rlib"}},
			action: func(ctx *TaskContext) {
				runs++

				if err := os.WriteFile(ctx.output(0), []byte("artifact"), 0o644); err != nil {
					t.Fatal(err)
				}
			},
		}
	}

	first := newTask()
	executor := runTasks([]*Task{first}, 1, root, false)
	installed := filepath.Join(root, "result", "artifact.rlib")
	executor.install(first, 0, installed)

	if data, err := os.ReadFile(installed); err != nil || string(data) != "artifact" {
		t.Fatalf("installed artifact = %q, %v", data, err)
	}

	second := newTask()
	runTasks([]*Task{second}, 1, root, false)

	if runs != 1 {
		t.Fatalf("task action ran %d times, want one", runs)
	}

	if first.state.uid != second.state.uid {
		t.Fatalf("cache UID changed: %q != %q", first.state.uid, second.state.uid)
	}
}

func TestTaskContextMaterializesOnlyRequestedTree(t *testing.T) {
	root := t.TempDir()
	producer := &Task{
		key:     "test|tree",
		name:    "tree",
		kind:    "RUN",
		outputs: []TaskOutput{{name: "out-dir", tree: true}},
		action: func(ctx *TaskContext) {
			path := filepath.Join(ctx.output(0), "generated.rs")

			if err := os.WriteFile(path, []byte("generated"), 0o644); err != nil {
				t.Fatal(err)
			}
		},
	}
	consumer := &Task{
		key:     "test|consumer",
		name:    "consumer",
		kind:    "RS",
		deps:    []*Task{producer},
		outputs: []TaskOutput{{name: "done"}},
		action: func(ctx *TaskContext) {
			data, err := os.ReadFile(filepath.Join(ctx.tree(producer, 0), "generated.rs"))

			if err != nil || string(data) != "generated" {
				t.Fatalf("materialized tree = %q, %v", data, err)
			}

			if err := os.WriteFile(ctx.output(0), []byte("done"), 0o644); err != nil {
				t.Fatal(err)
			}
		},
	}

	runTasks([]*Task{consumer}, 1, root, false)
}

func TestInstallDoesNotChangeSharedCasMode(t *testing.T) {
	root := t.TempDir()
	makeTask := func(key string, executable bool) *Task {
		return &Task{
			key: key, kind: "TEST",
			outputs: []TaskOutput{{name: key, executable: executable}},
			action: func(ctx *TaskContext) {
				if err := os.WriteFile(ctx.output(0), []byte("same contents"), 0o644); err != nil {
					t.Fatal(err)
				}
			},
		}
	}
	executable := makeTask("executable", true)
	regular := makeTask("regular", false)
	executor := runTasks([]*Task{executable, regular}, 1, root, false)

	if executable.state.manifest["executable"].Cas != regular.state.manifest["regular"].Cas {
		t.Fatal("identical outputs did not share CAS contents")
	}

	installed := filepath.Join(root, "installed", "regular")
	executor.install(regular, 0, installed)
	cas := executor.casPath(executable.state.manifest["executable"].Cas)
	casInfo := throw2(os.Stat(cas))
	installedInfo := throw2(os.Stat(installed))

	if casInfo.Mode().Perm() != 0o755 {
		t.Fatalf("CAS executable mode = %o, want 755", casInfo.Mode().Perm())
	}
	if installedInfo.Mode().Perm() != 0o644 {
		t.Fatalf("installed regular mode = %o, want 644", installedInfo.Mode().Perm())
	}
}

func TestSiblingCompilerFindsFlatBuildLayout(t *testing.T) {
	bin := filepath.Join(t.TempDir(), "bin")
	if err := os.MkdirAll(bin, 0o755); err != nil {
		t.Fatal(err)
	}

	rustc := filepath.Join(bin, "rustc")
	if err := os.WriteFile(rustc, nil, 0o755); err != nil {
		t.Fatal(err)
	}

	got := siblingCompilerAt(filepath.Join(bin, "cargo"), filepath.Join(t.TempDir(), "cas", "cargo"))
	if got != rustc {
		t.Fatalf("sibling compiler = %q, want %q", got, rustc)
	}
}

func TestSiblingCompilerKeepsInstalledLayout(t *testing.T) {
	root := t.TempDir()
	rustc := filepath.Join(root, "bin", "rustc", "rustc")
	if err := os.MkdirAll(filepath.Dir(rustc), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(rustc, nil, 0o755); err != nil {
		t.Fatal(err)
	}

	got := siblingCompilerAt(filepath.Join(root, "bin", "cargo", "cargo"), filepath.Join(t.TempDir(), "cas", "cargo"))
	if got != rustc {
		t.Fatalf("sibling compiler = %q, want %q", got, rustc)
	}
}

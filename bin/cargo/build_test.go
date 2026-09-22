package main

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
	"syscall"
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
	roots, _, _ := builder.rootTasks()

	if len(roots) != 1 {
		t.Fatalf("root task count = %d, want one", len(roots))
	}

	testUnit := builder.units[roots[0]]
	libraryTask := builder.libraryTask(pkg, true)
	linkedLibrary := builder.finalTask(libraryTask)

	if testUnit == nil || testUnit.rs == nil {
		t.Fatal("test compile unit is missing")
	}
	if got := targetCompileName(testUnit.target); got != library.name {
		t.Fatalf("test crate name = %q, want %q", got, library.name)
	}
	if !containsTask(testUnit.rs.deps, linkedLibrary) {
		t.Fatal("proc-macro test does not depend on the linked library")
	}
	if containsTask(testUnit.rs.deps, libraryTask) {
		t.Fatal("proc-macro test depends on metadata without the linked library")
	}
}

func TestLibraryTestCompilesSourceWithoutExternalSelf(t *testing.T) {
	root := t.TempDir()
	source := filepath.Join(root, "src", "lib.rs")

	if err := os.MkdirAll(filepath.Dir(source), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(source, nil, 0o644); err != nil {
		t.Fatal(err)
	}

	library := &Target{
		kind: "lib", name: "library", path: "src/lib.rs", test: true,
	}
	pkg := &Package{
		dir: root, manifestPath: filepath.Join(root, "Cargo.toml"), name: "library",
		version: Version{major: 1}, targets: []*Target{library},
		activeFeatures: map[string]bool{},
	}
	context := &BuildContext{
		opts: BuildOptions{command: "test", profile: "debug", targetDir: filepath.Join(root, "target")},
		root: pkg, workspace: &Workspace{dir: root}, host: "host", target: "host",
	}
	builder := &Builder{context: context, tasks: map[string]*Task{}, units: map[*Task]*CompileUnit{}}
	roots, _, _ := builder.rootTasks()

	if len(roots) != 1 {
		t.Fatalf("root task count = %d, want one", len(roots))
	}

	testUnit := builder.units[roots[0]]
	libraryTask := builder.libraryTask(pkg, true)

	if testUnit == nil || testUnit.rs == nil {
		t.Fatal("test compile unit is missing")
	}
	if got := targetCompileName(testUnit.target); got != library.name {
		t.Fatalf("test crate name = %q, want %q", got, library.name)
	}
	if containsTask(testUnit.rs.deps, libraryTask) {
		t.Fatal("library test depends on a second compiled copy of itself")
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

func TestRunRetryingTextBusyRetriesOnlyThatError(t *testing.T) {
	calls := 0
	err := runRetryingTextBusy(func() error {
		calls++
		if calls < 3 {
			return &os.PathError{Op: "fork/exec", Path: "bin", Err: syscall.ETXTBSY}
		}
		return nil
	})
	if err != nil || calls != 3 {
		t.Fatalf("expected success after 3 attempts, got err=%v calls=%d", err, calls)
	}

	calls = 0
	other := errors.New("other")
	err = runRetryingTextBusy(func() error {
		calls++
		return other
	})
	if !errors.Is(err, other) || calls != 1 {
		t.Fatalf("expected the other error at once, got err=%v calls=%d", err, calls)
	}
}

func TestCdylibLibraryLinksASharedLibraryBesideItsRlib(t *testing.T) {
	root := t.TempDir()
	source := filepath.Join(root, "src", "lib.rs")

	if err := os.MkdirAll(filepath.Dir(source), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(source, nil, 0o644); err != nil {
		t.Fatal(err)
	}

	library := &Target{
		kind: "lib", name: "dylib_dep", path: "src/lib.rs", crateTypes: []string{"cdylib", "rlib"},
	}
	pkg := &Package{
		dir: root, manifestPath: filepath.Join(root, "Cargo.toml"), name: "dylib-dep",
		version: Version{major: 1}, targets: []*Target{library},
		activeFeatures: map[string]bool{},
	}
	context := &BuildContext{
		opts: BuildOptions{command: "build", profile: "debug", targetDir: filepath.Join(root, "target")},
		root: pkg, workspace: &Workspace{dir: root}, host: "host", target: "host",
	}
	builder := &Builder{context: context, tasks: map[string]*Task{}, units: map[*Task]*CompileUnit{}}
	_, artifacts, _ := builder.rootTasks()

	if got := crateType(library); got != "rlib" {
		t.Fatalf("cdylib compiles as %q, want rlib", got)
	}

	unit := builder.units[builder.libraryTask(pkg, true)]
	if unit == nil || unit.metadata < 0 || !strings.HasSuffix(unit.rs.outputs[unit.metadata].name, ".rlib") {
		t.Fatal("cdylib compile unit has no rlib metadata output")
	}

	shared := filepath.Join(root, "target", "debug", "libdylib_dep"+sharedLibrarySuffix())
	found := false
	for _, artifact := range artifacts {
		if artifact.path == shared {
			found = true
			if artifact.task.kind != "LD" {
				t.Fatalf("shared library comes from a %q task, want LD", artifact.task.kind)
			}
			if artifact.task == builder.finalTask(unit.rs) {
				t.Fatal("shared library is the rlib's own final task")
			}
		}
	}
	if !found {
		t.Fatalf("no artifact installs %s: %v", shared, artifactPaths(artifacts))
	}
}

func artifactPaths(artifacts []InstallArtifact) []string {
	var paths []string
	for _, artifact := range artifacts {
		paths = append(paths, artifact.path)
	}

	return paths
}

// A package whose example and integration test both stand beside a
// dev-dependency: the shape clap has, where `examples/repl.rs` uses `shlex`
// and `tests/ui.rs` runs the `stdio-fixture` program.
func devDependentPackage(t *testing.T) *Package {
	t.Helper()

	base := t.TempDir()
	root := filepath.Join(base, "demo")
	helperDir := filepath.Join(base, "helper")

	for _, name := range []string{
		"demo/src/lib.rs", "demo/src/bin/prog.rs", "demo/examples/ex.rs", "demo/tests/ui.rs",
		"helper/src/lib.rs",
	} {
		path := filepath.Join(base, filepath.FromSlash(name))

		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, nil, 0o644); err != nil {
			t.Fatal(err)
		}
	}

	helper := &Package{
		dir: helperDir, manifestPath: filepath.Join(helperDir, "Cargo.toml"),
		name: "helper", version: Version{major: 1}, activeFeatures: map[string]bool{},
		targets: []*Target{{kind: "lib", name: "helper", path: "src/lib.rs"}},
	}

	return &Package{
		dir: root, manifestPath: filepath.Join(root, "Cargo.toml"), name: "demo",
		version: Version{major: 1}, activeFeatures: map[string]bool{},
		targets: []*Target{
			{kind: "lib", name: "demo", path: "src/lib.rs", test: true},
			{kind: "bin", name: "prog", path: "src/bin/prog.rs", test: true},
			{kind: "example", name: "ex", path: "examples/ex.rs"},
			{kind: "test", name: "ui", path: "tests/ui.rs", test: true, harness: true},
		},
		dependencies: Dependencies{
			dev: []*Dependency{{key: "helper", name: "helper", packageRef: helper}},
		},
	}
}

func builderFor(pkg *Package, opts BuildOptions) *Builder {
	opts.profile = "debug"
	opts.targetDir = filepath.Join(pkg.dir, "target")
	context := &BuildContext{
		opts: opts, root: pkg, workspace: &Workspace{dir: pkg.dir}, host: "host", target: "host",
	}

	return &Builder{context: context, tasks: map[string]*Task{}, units: map[*Task]*CompileUnit{}}
}

// An example links the package's dev-dependencies and a library does not:
// Cargo drops a dependency that is not transitive unless the unit is a test
// target, an example target or is compiled in a test mode.
func TestExampleLinksDevDependenciesAndALibraryDoesNot(t *testing.T) {
	pkg := devDependentPackage(t)
	builder := builderFor(pkg, BuildOptions{command: "build", selectors: TargetSelectors{examples: true}})
	roots, _, _ := builder.rootTasks()

	if len(roots) != 1 {
		t.Fatalf("root task count = %d, want the one example", len(roots))
	}

	helper := pkg.dependencies.dev[0].packageRef
	helperTask := builder.libraryTask(helper, true)
	example := builder.units[roots[0]]

	if example == nil || example.target.kind != "example" {
		t.Fatalf("root task is not the example: %v", roots[0].name)
	}
	if !containsTask(example.rs.deps, helperTask) {
		t.Fatal("example does not link the dev-dependency it is allowed to use")
	}
	if library := builder.libraryTask(pkg, true); containsTask(library.deps, helperTask) {
		t.Fatal("library links a dev-dependency")
	}
}

// Resolving stops before a dev-dependency unless the build can reach a unit
// that links one, so a plain `cargo build` does not pull their features into
// the graph - but a build of the examples does.
func TestDevDependenciesResolveForTheBuildsThatReachThem(t *testing.T) {
	for _, entry := range []struct {
		opts BuildOptions
		want bool
	}{
		{BuildOptions{command: "build"}, false},
		{BuildOptions{command: "build", selectors: TargetSelectors{examples: true}}, true},
		{BuildOptions{command: "build", selectors: TargetSelectors{example: []string{"ex"}}}, true},
		{BuildOptions{command: "build", selectors: TargetSelectors{tests: true}}, true},
		{BuildOptions{command: "test"}, true},
	} {
		if got := buildsDevDependents(entry.opts); got != entry.want {
			t.Fatalf("buildsDevDependents(%+v) = %v, want %v", entry.opts, got, entry.want)
		}
	}
}

// `cargo test` keeps a harness in `target/<profile>/deps` and the program the
// same source also makes in `target/<profile>`, so an integration test that
// runs the package's program finds the program.
func TestTestHarnessLeavesTheProgramItsOwnName(t *testing.T) {
	pkg := devDependentPackage(t)
	builder := builderFor(pkg, BuildOptions{command: "test"})
	_, artifacts, reports := builder.rootTasks()

	outputDir := builder.outputDir(true)
	program := filepath.Join(outputDir, "prog"+executableSuffix())
	example := filepath.Join(outputDir, "examples", "ex"+executableSuffix())
	deps := filepath.Join(outputDir, "deps")
	installed := map[string]InstallArtifact{}

	for _, artifact := range artifacts {
		if _, seen := installed[artifact.path]; seen {
			t.Fatalf("two artifacts install %s", artifact.path)
		}

		installed[artifact.path] = artifact
	}

	for path, artifact := range installed {
		if path == program || path == example {
			continue
		}
		if filepath.Dir(path) != deps {
			t.Fatalf("test harness %s stands outside %s", path, deps)
		}
		if !artifact.binary {
			t.Fatalf("test harness %s is not run as a test", path)
		}
	}

	if _, built := installed[program]; !built {
		t.Fatalf("no artifact installs the program %s: %v", program, artifactPaths(artifacts))
	}
	if installed[program].binary {
		t.Fatal("the program is run as if it were a test harness")
	}

	harnesses := 0

	for _, report := range reports {
		if report.testProfile {
			harnesses++
		}
	}

	if want := 3; harnesses != want {
		t.Fatalf("reported %d harnesses, want %d (lib, bin, integration test)", harnesses, want)
	}
}

// `cargo test` builds every example as the program it is, to check that it
// compiles (`new_units`, cargo/ops/cargo_compile/unit_generator.rs), and puts
// it in `target/<profile>/examples`, where once_cell's `reentrant_init` runs
// `reentrant_init_deadlocks` from. An example is not a harness and is not run.
func TestATestRunBuildsTheExamplesAsPrograms(t *testing.T) {
	pkg := devDependentPackage(t)
	builder := builderFor(pkg, BuildOptions{command: "test"})
	_, artifacts, reports := builder.rootTasks()

	example := filepath.Join(builder.outputDir(true), "examples", "ex"+executableSuffix())
	var installed *InstallArtifact

	for i := range artifacts {
		if artifacts[i].path == example {
			installed = &artifacts[i]
		}
	}

	if installed == nil {
		t.Fatalf("no artifact installs the example %s: %v", example, artifactPaths(artifacts))
	}
	if installed.binary {
		t.Fatal("the example is run as if it were a test harness")
	}
	reported := false

	for _, report := range reports {
		if report.target.kind != "example" {
			continue
		}
		if report.testProfile {
			t.Fatal("the example is reported as a test harness")
		}
		reported = reported || report.path == example
	}

	if !reported {
		t.Fatal("the example is not reported as the program it is")
	}
}

// A package with no integration test has no reason to build its programs
// twice.
func TestABinOnlyTestRunBuildsTheProgramOnlyAsAHarness(t *testing.T) {
	pkg := devDependentPackage(t)
	builder := builderFor(pkg, BuildOptions{
		command: "test", selectors: TargetSelectors{bin: []string{"prog"}},
	})
	_, artifacts, _ := builder.rootTasks()

	if len(artifacts) != 1 {
		t.Fatalf("artifact count = %d, want the one harness: %v", len(artifacts), artifactPaths(artifacts))
	}
	if dir := filepath.Dir(artifacts[0].path); dir != filepath.Join(builder.outputDir(true), "deps") {
		t.Fatalf("harness stands in %s", dir)
	}
}

// A build that asks for a message stream must not thereby compile the world a
// second time. `--message-format` selects how what the compiler said is
// written down, not what is compiled, so it has no place in the fingerprint
// that decides whether a unit is fresh.
func TestTheMessageFormatIsNotPartOfAUnitFingerprint(t *testing.T) {
	pkg := devDependentPackage(t)
	plain := builderFor(pkg, BuildOptions{command: "build"})
	stream := builderFor(pkg, BuildOptions{command: "build", messageFormat: "json"})
	library := packageLibrary(pkg)

	got := strings.Join(stream.rustSignature(pkg, library, true), "\x00")
	want := strings.Join(plain.rustSignature(pkg, library, true), "\x00")

	if got != want {
		t.Fatalf("signature differs with a message stream:\n%q\n%q", got, want)
	}
}

// What the compiler said is an output of the unit, so a build that found the
// unit in the cache can say it again instead of compiling to hear it.
func TestACompileUnitKeepsWhatTheCompilerSaid(t *testing.T) {
	pkg := devDependentPackage(t)
	builder := builderFor(pkg, BuildOptions{command: "build"})
	builder.rootTasks()
	unit := builder.units[builder.libraryTask(pkg, true)]

	if unit == nil || unit.diag < 0 || unit.diag >= len(unit.rs.outputs) {
		t.Fatal("compile unit has no diagnostics output")
	}
	if name := unit.rs.outputs[unit.diag].name; !strings.HasSuffix(name, ".diag") {
		t.Fatalf("diagnostics output is %q", name)
	}
	if unit.rs.after == nil {
		t.Fatal("a compile unit does not replay what the compiler said")
	}
}

// How many jobs the caller allowed is not an input to what a build script
// produces. A test that spawns a nested `cargo build` does so without `-j`,
// and everything downstream of a build script would be built a second time if
// that number reached the fingerprint.
func TestTheJobCountIsNotPartOfABuildScriptFingerprint(t *testing.T) {
	pkg := devDependentPackage(t)
	pkg.buildScript = "build.rs"
	few := builderFor(pkg, BuildOptions{command: "build", jobs: 24})
	many := builderFor(pkg, BuildOptions{command: "build", jobs: 78})
	few.context.cfg = &CfgSet{}
	many.context.cfg = &CfgSet{}

	got := strings.Join(many.buildScriptRunSignature(pkg), "\x00")
	want := strings.Join(few.buildScriptRunSignature(pkg), "\x00")

	if got != want {
		t.Fatalf("signature differs with the job count:\n%q\n%q", got, want)
	}
}

package main

import (
	"errors"
	"os"
	"path/filepath"
	"sort"
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
	libraryTask := builder.libraryTask(pkg, true, false)
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
	libraryTask := builder.libraryTask(pkg, true, false)

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

	unit := builder.units[builder.libraryTask(pkg, true, false)]
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
	helperTask := builder.libraryTask(helper, true, false)
	example := builder.units[roots[0]]

	if example == nil || example.target.kind != "example" {
		t.Fatalf("root task is not the example: %v", roots[0].name)
	}
	if !containsTask(example.rs.deps, helperTask) {
		t.Fatal("example does not link the dev-dependency it is allowed to use")
	}
	if library := builder.libraryTask(pkg, true, false); containsTask(library.deps, helperTask) {
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

	got := strings.Join(stream.rustSignature(pkg, library, true, stream.unitProfile(pkg, library, false)), "\x00")
	want := strings.Join(plain.rustSignature(pkg, library, true, plain.unitProfile(pkg, library, false)), "\x00")

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
	unit := builder.units[builder.libraryTask(pkg, true, false)]

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

// base64's `[profile.test] opt-level = 3` builds its tests optimized, with
// the debug assertions and overflow checks `test` inherits from `dev`; the
// flags passed are the ones that differ from the compiler's defaults at
// that level.
func TestTestProfileInheritsDevAndTakesTheManifestTable(t *testing.T) {
	root := t.TempDir()
	manifest := filepath.Join(root, "Cargo.toml")
	text := "[package]\nname = \"p\"\nversion = \"0.1.0\"\n\n[profile.test]\nopt-level = 3\n\n[profile.fast]\ninherits = \"release\"\ndebug-assertions = true\n"

	if err := os.WriteFile(manifest, []byte(text), 0o644); err != nil {
		t.Fatal(err)
	}
	workspace := findWorkspace(manifest)

	if name := profileName(BuildOptions{command: "test", profile: "debug"}); name != "test" {
		t.Fatalf("cargo test selects %q, want test", name)
	}
	test := resolveProfile(workspace, "test")
	if test.optLevel != "3" || !test.debug || !test.debugAssertions || !test.overflowChecks {
		t.Fatalf("test profile = %+v", test)
	}
	got := strings.Join(profileCompilerArgs(test), " ")
	if got != "-C opt-level=3 -g -C debug-assertions=on --cfg debug_assertions" {
		t.Fatalf("test profile flags = %q", got)
	}
	dev := strings.Join(profileCompilerArgs(resolveProfile(workspace, "dev")), " ")
	if dev != "-g --cfg debug_assertions" {
		t.Fatalf("dev profile flags = %q", dev)
	}
	fast := resolveProfile(workspace, "fast")
	if fast.optLevel != "3" || fast.debug || !fast.debugAssertions || fast.overflowChecks {
		t.Fatalf("custom profile = %+v", fast)
	}
}

// indoc's unit tests build its proc-macro library as a test binary, where
// only Cargo's `--extern proc_macro` puts `proc_macro` in the extern prelude.
func TestProcMacroTargetImportsProcMacro(t *testing.T) {
	library := &Target{kind: "lib", name: "m", procMacro: true, test: true}
	if got := strings.Join(procMacroPreludeArgs(library), " "); got != "--extern proc_macro" {
		t.Fatalf("proc-macro target args = %q", got)
	}
	plain := &Target{kind: "lib", name: "p", test: true}
	if got := procMacroPreludeArgs(plain); len(got) != 0 {
		t.Fatalf("plain library args = %q", got)
	}
}

// string_cache depends on `new_debug_unreachable`, whose library target is
// named `debug_unreachable`, and writes `use debug_unreachable::..`. Cargo names
// a dependency in the extern prelude after its library target, and after the
// dependency's key only when `package = ".."` renames it.
func TestExternNameIsTheLibraryTargetUnlessRenamed(t *testing.T) {
	lib := &Target{kind: "lib", name: "debug_unreachable"}

	if got := externCrateName(&Dependency{key: "new_debug_unreachable", name: "new_debug_unreachable"}, lib); got != "debug_unreachable" {
		t.Fatalf("unrenamed dependency extern name = %q, want the library target's", got)
	}
	if got := externCrateName(&Dependency{key: "dbg-unreachable", name: "new_debug_unreachable"}, lib); got != "dbg_unreachable" {
		t.Fatalf("renamed dependency extern name = %q, want its key", got)
	}
}

// rustc_apfloat's `[package] version.workspace = true` takes the version its
// workspace's `[workspace.package]` sets, and its build script checks the
// `+llvm-..` build metadata of CARGO_PKG_VERSION.
func TestPackageVersionInheritsTheWorkspaces(t *testing.T) {
	workspace := &Workspace{packageTable: map[string]any{"version": "0.2.3+llvm-462a31f5a5ab"}}
	doc := map[string]any{"package": map[string]any{
		"name": "rustc_apfloat", "version": map[string]any{"workspace": true},
	}}

	if got := parsePackage(filepath.Join(t.TempDir(), "Cargo.toml"), doc, workspace).version.string(); got != "0.2.3+llvm-462a31f5a5ab" {
		t.Fatalf("inherited version = %q", got)
	}
}

// RustCrypto/traits excludes `digest` from its root workspace; digest is then
// the root of its own, with its own Cargo.lock (cargo's `is_excluded`: a path
// under an `exclude` entry and under no `members` entry).
func TestAnExcludedPackageIsItsOwnWorkspaceRoot(t *testing.T) {
	root := t.TempDir()
	digest := filepath.Join(root, "digest", "Cargo.toml")
	aead := filepath.Join(root, "aead", "Cargo.toml")

	for path, text := range map[string]string{
		filepath.Join(root, "Cargo.toml"): "[workspace]\nmembers = [\"aead\"]\nexclude = [\"digest\"]\n",
		digest:                            "[package]\nname = \"digest\"\nversion = \"0.10.7\"\n",
		aead:                              "[package]\nname = \"aead\"\nversion = \"0.5.0\"\n",
	} {
		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte(text), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	if got := findWorkspace(digest).manifestPath; got != digest {
		t.Fatalf("excluded package's workspace = %q, want its own manifest", got)
	}
	if got := findWorkspace(aead).manifestPath; got != filepath.Join(root, "Cargo.toml") {
		t.Fatalf("member's workspace = %q, want the root", got)
	}
}

// A test binary may run the compiler itself (autocfg's probes, libloading's
// helper library): with rustc its sysroot comes along, and ours is the library
// directory a build script is given as well.
func TestTestProcessesFindTheLibraryDirectory(t *testing.T) {
	root := t.TempDir()
	pkg := &Package{dir: root, manifestPath: filepath.Join(root, "Cargo.toml"), name: "probe", activeFeatures: map[string]bool{}}
	context := &BuildContext{
		opts: BuildOptions{command: "test", profile: "debug", libSearch: []string{filepath.Join(root, "lib")}},
		root: pkg, workspace: &Workspace{dir: root}, host: "host", target: "host",
	}
	builder := &Builder{context: context, tasks: map[string]*Task{}, units: map[*Task]*CompileUnit{}}

	if got := builder.testEnv()["TRUSTME_LIBDIR"]; got != filepath.Join(root, "lib") {
		t.Fatalf("TRUSTME_LIBDIR = %q", got)
	}
}

func TestAnIntegrationTestCompilesWithATargetTmpdir(t *testing.T) {
	root := t.TempDir()
	pkg := &Package{dir: root, manifestPath: filepath.Join(root, "Cargo.toml"), name: "probe", activeFeatures: map[string]bool{}}
	context := &BuildContext{
		opts: BuildOptions{command: "test", profile: "debug", targetDir: filepath.Join(root, "target")},
		root: pkg, workspace: &Workspace{dir: root}, host: "host", target: "host",
	}
	builder := &Builder{context: context, tasks: map[string]*Task{}, units: map[*Task]*CompileUnit{}}

	if got := builder.targetTmpDir(&Target{kind: "test"}, true); got != filepath.Join(root, "target", "tmp") {
		t.Fatalf("integration test CARGO_TARGET_TMPDIR = %q", got)
	}
	if got := builder.targetTmpDir(&Target{kind: "bench"}, true); got != filepath.Join(root, "target", "tmp") {
		t.Fatalf("bench CARGO_TARGET_TMPDIR = %q", got)
	}
	if got := builder.targetTmpDir(&Target{kind: "test", libraryTest: true}, true); got != "" {
		t.Fatalf("library test CARGO_TARGET_TMPDIR = %q, want none", got)
	}
}

func TestALibraryRlibStandsInDeps(t *testing.T) {
	root := t.TempDir()
	source := filepath.Join(root, "src", "lib.rs")

	if err := os.MkdirAll(filepath.Dir(source), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(source, nil, 0o644); err != nil {
		t.Fatal(err)
	}

	library := &Target{kind: "lib", name: "probe", path: "src/lib.rs", crateTypes: []string{"lib"}}
	pkg := &Package{
		dir: root, manifestPath: filepath.Join(root, "Cargo.toml"), name: "probe",
		version: Version{major: 1}, targets: []*Target{library},
		activeFeatures: map[string]bool{},
	}
	context := &BuildContext{
		opts: BuildOptions{command: "build", profile: "debug", targetDir: filepath.Join(root, "target")},
		root: pkg, workspace: &Workspace{dir: root}, host: "host", target: "host",
	}
	builder := &Builder{context: context, tasks: map[string]*Task{}, units: map[*Task]*CompileUnit{}}
	builder.rootTasks()

	unit := builder.units[builder.libraryTask(pkg, true, false)]
	deps := filepath.Join(root, "target", "debug", "deps", "lib"+builder.crateName(unit)+".rlib")
	paths := artifactPaths(builder.depsArtifacts())

	for _, want := range []string{deps, deps + ".o"} {
		if !contains(paths, want) {
			t.Fatalf("no artifact installs %s: %v", want, paths)
		}
	}
	if !strings.HasPrefix(filepath.Base(deps), "libprobe-") {
		t.Fatalf("deps rlib %s is not found by `lib<name>-*.rlib`", deps)
	}
}

// ui_test builds its dependencies with `cargo build --message-format=json` and
// takes each dependency's library from the stream: a package's dependency is
// reported by its rlib in `deps`, the package's own units as before.
func TestADependencyLibraryIsReportedAsAnArtifact(t *testing.T) {
	pkg := devDependentPackage(t)
	pkg.dependencies.main = pkg.dependencies.dev
	pkg.dependencies.dev = nil
	builder := builderFor(pkg, BuildOptions{command: "build", selectors: TargetSelectors{lib: true}})
	builder.rootTasks()

	reports := builder.dependencyReports(builder.depsArtifacts())
	if len(reports) != 1 || reports[0].pkg.name != "helper" || filepath.Base(filepath.Dir(reports[0].path)) != "deps" {
		t.Fatalf("dependency reports = %+v", reports)
	}
}

// ui_test builds its dependency crate with `cargo build` and links examples
// against what lands in `deps`: a build compiles every library it needs to a
// complete rlib, the object of one nothing in the build links included.
func TestABuildCompilesTheObjectOfEveryDependencyLibrary(t *testing.T) {
	pkg := devDependentPackage(t)
	pkg.dependencies.main = pkg.dependencies.dev
	pkg.dependencies.dev = nil
	builder := builderFor(pkg, BuildOptions{command: "build", selectors: TargetSelectors{lib: true}})
	builder.rootTasks()
	roots := builder.libraryObjectTasks()

	helper := builder.units[builder.libraryTask(pkg.dependencies.main[0].packageRef, true, false)]
	rooted := false

	for _, root := range roots {
		rooted = rooted || helper.cc != nil && root == helper.cc
	}

	if !rooted {
		t.Fatalf("roots %v do not compile the object of %s", roots, helper.pkg.name)
	}

	object := filepath.Join(builder.outputDir(true), "deps", "lib"+builder.crateName(helper)+".rlib.o")
	if !contains(artifactPaths(builder.depsArtifacts()), object) {
		t.Fatalf("no artifact installs %s", object)
	}
}

// ui_test's examples use futures, whose `join!` expands through the
// futures-macro plugin: a proc macro's metadata and its plugin both stand in
// `deps`, where a dependent that names the macro crate finds them.
func TestAProcMacroStandsInDepsWithItsPlugin(t *testing.T) {
	root := t.TempDir()
	source := filepath.Join(root, "src", "lib.rs")

	if err := os.MkdirAll(filepath.Dir(source), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(source, nil, 0o644); err != nil {
		t.Fatal(err)
	}

	library := &Target{kind: "lib", name: "derive_it", path: "src/lib.rs", crateTypes: []string{"proc-macro"}, procMacro: true}
	pkg := &Package{
		dir: root, manifestPath: filepath.Join(root, "Cargo.toml"), name: "derive-it",
		version: Version{major: 1}, targets: []*Target{library},
		activeFeatures: map[string]bool{},
	}
	context := &BuildContext{
		opts: BuildOptions{command: "build", profile: "debug", targetDir: filepath.Join(root, "target")},
		root: pkg, workspace: &Workspace{dir: root}, host: "host", target: "host",
	}
	builder := &Builder{context: context, tasks: map[string]*Task{}, units: map[*Task]*CompileUnit{}}
	builder.rootTasks()

	unit := builder.units[builder.libraryTask(pkg, true, false)]
	metadata := filepath.Join(root, "target", "debug", "deps", "lib"+builder.crateName(unit)+".rlib")
	paths := artifactPaths(builder.depsArtifacts())

	for _, want := range []string{metadata, strings.TrimSuffix(metadata, ".rlib")} {
		if !contains(paths, want) {
			t.Fatalf("no artifact installs %s: %v", want, paths)
		}
	}
}

func TestAWorkspaceMemberIsCompiledFromTheWorkspaceRoot(t *testing.T) {
	root := t.TempDir()
	vendor := filepath.Join(root, "vendor")
	member := &Package{dir: filepath.Join(root, "member"), name: "member"}
	vendored := &Package{dir: filepath.Join(vendor, "dep"), name: "dep"}
	outside := &Package{dir: filepath.Join(filepath.Dir(root), "sibling"), name: "sibling"}
	context := &BuildContext{
		workspace:  &Workspace{dir: root},
		repository: &Repository{vendorDir: vendor},
	}
	builder := &Builder{context: context}

	for _, want := range []struct {
		pkg    *Package
		source string
		dir    string
	}{
		{member, filepath.Join("member", "tests", "integration", "main.rs"), root},
		{vendored, filepath.Join(vendored.dir, "tests", "integration", "main.rs"), vendored.dir},
		{outside, filepath.Join(outside.dir, "tests", "integration", "main.rs"), outside.dir},
	} {
		source, dir := builder.sourceArgs(want.pkg, filepath.Join(want.pkg.dir, "tests", "integration", "main.rs"))

		if source != want.source || dir != want.dir {
			t.Fatalf("%s compiles %q from %q, want %q from %q", want.pkg.name, source, dir, want.source, want.dir)
		}
	}
}

// phf's library has `test = false` and the package nothing else to test:
// cargo still makes a `Doctest` unit of the library, and that needs the
// library built (`generate_root_units`, cargo/ops/cargo_compile/unit_generator.rs).
func TestATestRunWithOnlyDocTestsBuildsTheLibrary(t *testing.T) {
	root := t.TempDir()
	source := filepath.Join(root, "src", "lib.rs")

	if err := os.MkdirAll(filepath.Dir(source), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(source, nil, 0o644); err != nil {
		t.Fatal(err)
	}

	library := &Target{kind: "lib", name: "phf", path: "src/lib.rs", test: false, doctest: true}
	pkg := &Package{
		dir: root, manifestPath: filepath.Join(root, "Cargo.toml"), name: "phf",
		version: Version{major: 1}, targets: []*Target{library},
		activeFeatures: map[string]bool{},
	}
	context := &BuildContext{
		opts: BuildOptions{command: "test", profile: "debug", targetDir: filepath.Join(root, "target")},
		root: pkg, workspace: &Workspace{dir: root}, host: "host", target: "host",
	}
	builder := &Builder{context: context, tasks: map[string]*Task{}, units: map[*Task]*CompileUnit{}}
	roots, artifacts, _ := builder.rootTasks()

	if len(roots) != 1 || roots[0] != builder.finalTask(builder.libraryTask(pkg, true, false)) {
		t.Fatalf("root tasks = %d, want the library alone", len(roots))
	}
	if len(artifacts) != 0 {
		t.Fatalf("a test run installs %d artifacts for a library it runs no test of", len(artifacts))
	}
}

// prettyplease's build script asserts that `CARGO_MANIFEST_LINKS` is the
// manifest's `links`, which cargo sets for a package that has one.
func TestABuildScriptIsToldItsPackagesLinks(t *testing.T) {
	pkg := &Package{name: "prettyplease", links: "prettyplease03", activeFeatures: map[string]bool{"verbatim": true}}
	env := buildScriptPackageEnv(pkg)

	if env["CARGO_MANIFEST_LINKS"] != "prettyplease03" {
		t.Fatalf("CARGO_MANIFEST_LINKS = %q, want the manifest's links", env["CARGO_MANIFEST_LINKS"])
	}
	if env["CARGO_FEATURE_VERBATIM"] != "1" {
		t.Fatal("an active feature is not in the build script's environment")
	}
	if _, ok := buildScriptPackageEnv(&Package{name: "other"})["CARGO_MANIFEST_LINKS"]; ok {
		t.Fatal("a package without links is told it has some")
	}
}

// crypto-bigint's `[profile.dev] opt-level = 2` reached its proc macros and
// their dependencies too, and syn and serde_derive compiled at -O1 -g on the
// way to every test. Cargo compiles a for-host unit - a build script, a proc
// macro, whatever those depend on - at opt-level 0, and leaves its debug info
// deferred until the graph shows whether a runtime unit is the same: then the
// two are one unit, otherwise the host one goes without (`ProfileMaker::
// get_profile`, cargo/core/profiles.rs; `traverse_and_share`,
// cargo/ops/cargo_compile/mod.rs). `[profile.<name>.build-override]` comes last.
func hostGraph(t *testing.T, profiles map[string]any) (*Builder, map[string]*Package) {
	t.Helper()

	base := t.TempDir()
	pkgs := map[string]*Package{}

	for _, name := range []string{"app", "mac", "shared", "hostonly", "leaf"} {
		dir := filepath.Join(base, name)
		files := []string{"src/lib.rs"}

		if name == "hostonly" {
			files = append(files, "build.rs")
		}
		for _, file := range files {
			path := filepath.Join(dir, filepath.FromSlash(file))

			if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
				t.Fatal(err)
			}
			if err := os.WriteFile(path, nil, 0o644); err != nil {
				t.Fatal(err)
			}
		}
		pkgs[name] = &Package{
			dir: dir, manifestPath: filepath.Join(dir, "Cargo.toml"), name: name,
			version: Version{major: 1}, activeFeatures: map[string]bool{},
			targets: []*Target{{kind: "lib", name: name, path: "src/lib.rs", test: name == "app", procMacro: name == "mac"}},
		}
	}
	pkgs["hostonly"].buildScript = "build.rs"
	depend := func(from string, to ...string) {
		for _, name := range to {
			pkgs[from].dependencies.main = append(pkgs[from].dependencies.main, &Dependency{key: name, name: name, packageRef: pkgs[name]})
		}
	}
	depend("app", "mac", "shared")
	depend("mac", "shared", "hostonly")
	depend("hostonly", "leaf")

	app := pkgs["app"]
	context := &BuildContext{
		opts: BuildOptions{command: "test", profile: "debug", targetDir: filepath.Join(app.dir, "target")},
		root: app, workspace: &Workspace{dir: app.dir, profiles: profiles}, host: "host", target: "host", cfg: &CfgSet{},
	}
	builder := &Builder{context: context, tasks: map[string]*Task{}, units: map[*Task]*CompileUnit{}}
	builder.rootTasks()

	return builder, pkgs
}

func codegenFlags(builder *Builder, pkg *Package) []string {
	var flags []string

	for task, unit := range builder.units {
		if task == unit.rs && unit.pkg == pkg && unit.target.kind == "lib" {
			signature := builder.codegenTask(unit.rs).signature
			flags = append(flags, strings.Join(signature[1+len(builder.cxxSignature(unit.isHost)):], " "))
		}
	}
	sort.Strings(flags)

	return flags
}

func TestHostUnitsCompileFastUnlessARuntimeUnitIsTheSame(t *testing.T) {
	builder, pkgs := hostGraph(t, map[string]any{"dev": map[string]any{"opt-level": int64(2)}})

	for name, want := range map[string]string{
		"shared":   "-O0|-O1 -g",
		"mac":      "-O0",
		"hostonly": "-O0",
		"leaf":     "-O0",
	} {
		if got := strings.Join(codegenFlags(builder, pkgs[name]), "|"); got != want {
			t.Errorf("%s compiles its C++ with %q, want %q", name, got, want)
		}
	}
	names := map[string]bool{}

	for task, unit := range builder.units {
		if task == unit.rs && unit.pkg == pkgs["shared"] {
			names[builder.crateName(unit)] = true
		}
	}
	if len(names) != 2 {
		t.Errorf("the host and runtime units of one library are crates %v, want two of their own", names)
	}
	script := builder.codegenTask(builder.buildScriptCompileTask(pkgs["hostonly"])).signature
	if got := strings.Join(script[1+len(builder.cxxSignature(true)):], " "); got != "-O0" {
		t.Errorf("a build script compiles its C++ with %q, want -O0 without debug info", got)
	}

	builder, pkgs = hostGraph(t, nil)

	for name, want := range map[string]string{
		"shared": "-O0 -g",
		"mac":    "-O0",
		"leaf":   "-O0",
	} {
		if got := strings.Join(codegenFlags(builder, pkgs[name]), "|"); got != want {
			t.Errorf("at the dev profile's own opt-level %s compiles with %q, want %q", name, got, want)
		}
	}

	builder, pkgs = hostGraph(t, map[string]any{"dev": map[string]any{
		"opt-level": int64(2), "build-override": map[string]any{"opt-level": int64(1), "debug": true},
	}})

	if got := strings.Join(codegenFlags(builder, pkgs["mac"]), "|"); got != "-O1 -g" {
		t.Errorf("under a build-override the proc macro compiles with %q, want -O1 -g", got)
	}
}

// arti 2.7.0 is built as rustc 1.92 and nothing in our compiler is tied to
// one rustc release: cargo runs the compiler, and every build script, as the
// release its caller names, and a unit built for another release is another
// unit.
func TestTheCompilerRunsAsTheReleaseCargoIsGiven(t *testing.T) {
	t.Setenv("RUSTC_OVERRIDE_VERSION_STRING", "1.92.0")
	builder, pkgs := hostGraph(t, nil)
	app := pkgs["app"]

	if got := builder.commonEnv(app)["RUSTC_OVERRIDE_VERSION_STRING"]; got != "1.92.0" {
		t.Fatalf("the compiler is run as %q, want the 1.92.0 cargo is given", got)
	}
	signature := strings.Join(builder.rustSignature(app, packageLibrary(app), true, builder.unitProfile(app, packageLibrary(app), false)), " ")
	if !strings.Contains(signature, "rustc-version=1.92.0") {
		t.Fatalf("a unit's signature %q does not carry the release it is built as", signature)
	}
	if script := strings.Join(builder.buildScriptRunSignature(pkgs["hostonly"]), " "); !strings.Contains(script, "rustc-version=1.92.0") {
		t.Fatalf("a build script run's signature %q does not carry the release", script)
	}
}

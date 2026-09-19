package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"sort"
	"strings"
	"sync"
)

var cargoMessageMu sync.Mutex

// `build-finished` is written once per invocation. The build phase reports its
// own success, so a command that goes on to run what it built - `cargo test` -
// does not report a failing test as a second outcome of the build.
var buildFinishedOnce sync.Once

// The ndjson `--message-format=json` writes is a schema, not a log: a consumer
// deserializes every line into the shapes Cargo publishes, so a field left out
// does not make a shorter message but an unreadable one. escargot's
// `format::Message` - what trycmd's `compile_examples` reads to find the
// example binaries it runs - wants the whole target behind a
// `compiler-message`, and learns of a built binary only from a
// `compiler-artifact` and its `executable`.
type cargoCompilerMessage struct {
	Reason       string          `json:"reason"`
	PackageID    string          `json:"package_id"`
	ManifestPath string          `json:"manifest_path"`
	Target       MetadataTarget  `json:"target"`
	Message      cargoDiagnostic `json:"message"`
}

// Our compiler renders its own diagnostics, so `rendered` carries them and the
// structured fields stay empty - as they do upstream for a diagnostic that
// came with no span information.
type cargoDiagnostic struct {
	Message  string            `json:"message"`
	Code     any               `json:"code"`
	Level    string            `json:"level"`
	Spans    []any             `json:"spans"`
	Children []cargoDiagnostic `json:"children"`
	Rendered string            `json:"rendered"`
}

// One line per selected target of the package being built, naming where that
// target's files were installed - the paths a caller can run. Dependencies
// live in the content-addressed cache and are not reported: their files have
// no name a caller could have asked for.
type cargoCompilerArtifact struct {
	Reason       string               `json:"reason"`
	PackageID    string               `json:"package_id"`
	ManifestPath string               `json:"manifest_path"`
	Target       MetadataTarget       `json:"target"`
	Profile      cargoArtifactProfile `json:"profile"`
	Features     []string             `json:"features"`
	Filenames    []string             `json:"filenames"`
	Executable   any                  `json:"executable"`
	Fresh        bool                 `json:"fresh"`
}

type cargoArtifactProfile struct {
	OptLevel        string `json:"opt_level"`
	Debuginfo       any    `json:"debuginfo"`
	DebugAssertions bool   `json:"debug_assertions"`
	OverflowChecks  bool   `json:"overflow_checks"`
	Test            bool   `json:"test"`
}

// The line a consumer stops at: everything after it belongs to whatever the
// command does with what it built.
type cargoBuildFinished struct {
	Reason  string `json:"reason"`
	Success bool   `json:"success"`
}

func jsonMessages(opts BuildOptions) bool {
	return strings.HasPrefix(opts.messageFormat, "json") && !opts.dryRun
}

func (b *Builder) jsonMessages() bool {
	return jsonMessages(b.context.opts)
}

// A target as the message stream spells it: every list is present, because to
// a consumer expecting a sequence an absent list and a null are not the same
// value.
func messageTarget(pkg *Package, target *Target) MetadataTarget {
	if target.kind == "build-script" {
		return MetadataTarget{
			Kind:             []string{"custom-build"},
			CrateTypes:       []string{"bin"},
			Name:             "build-script-build",
			SrcPath:          absoluteFrom(pkg.dir, pkg.buildScript),
			Edition:          pkg.edition,
			RequiredFeatures: []string{},
		}
	}

	crateTypes := append([]string{}, target.crateTypes...)

	if len(crateTypes) == 0 {
		crateTypes = []string{crateType(target)}
	}

	return MetadataTarget{
		Kind:             []string{target.kind},
		CrateTypes:       crateTypes,
		Name:             target.name,
		SrcPath:          targetSourcePath(pkg, target),
		Edition:          target.edition,
		Doc:              target.doc,
		Doctest:          target.doctest,
		Test:             target.test,
		RequiredFeatures: append([]string{}, target.requiredFeatures...),
	}
}

func (b *Builder) artifactProfile(isTest bool) cargoArtifactProfile {
	if b.context.opts.profile == "release" {
		return cargoArtifactProfile{
			OptLevel: "3", Debuginfo: nil, DebugAssertions: false,
			OverflowChecks: false, Test: isTest,
		}
	}

	return cargoArtifactProfile{
		OptLevel: "0", Debuginfo: 2, DebugAssertions: true,
		OverflowChecks: true, Test: isTest,
	}
}

func activeFeatureNames(pkg *Package) []string {
	names := make([]string, 0, len(pkg.activeFeatures))

	for name, active := range pkg.activeFeatures {
		if active {
			names = append(names, name)
		}
	}

	sort.Strings(names)

	return names
}

// A tool cargo drives - the C++ compiler behind a codegen or a link task, a
// build script - writes into the message stream rather than onto this
// process's own streams when the build asked for one. Two reasons, either
// enough on its own: the stream is standard output, which a child writing
// there would corrupt; and a consumer reads the stream line by line and
// drains standard error only once the build has exited (escargot's
// `CommandMessages::next_msg`, which is how trycmd runs cargo), so a tool
// writing more than a pipeful there deadlocks the build it belongs to. Half a
// megabyte of C++ diagnostics per nested build is routine.
func (b *Builder) runTool(dir string, env map[string]string, logPath string, pkg *Package, target *Target, name string, args ...string) {
	if !b.jsonMessages() {
		runCommand(dir, env, logPath, b.context.opts.dryRun, name, args...)

		return
	}

	var output bytes.Buffer

	exc := try(func() {
		runCommandTo(dir, env, logPath, b.context.opts.dryRun, &output, name, args...)
	})

	if output.Len() > 0 {
		level := "warning"

		if exc != nil {
			level = "error"
		}

		b.emitDiagnostic(pkg, target, level, output.String())
	}

	if exc != nil {
		exc.throw()
	}
}

func (b *Builder) emitDiagnostic(pkg *Package, target *Target, level string, rendered string) {
	emitCargoMessage(cargoCompilerMessage{
		Reason:       "compiler-message",
		PackageID:    metadataPackageID(pkg),
		ManifestPath: pkg.manifestPath,
		Target:       messageTarget(pkg, target),
		Message: cargoDiagnostic{
			Message:  strings.TrimRight(rendered, "\n"),
			Level:    level,
			Spans:    []any{},
			Children: []cargoDiagnostic{},
			Rendered: rendered,
		},
	})
}

// What the compiler said is an output of compiling the crate, kept in
// `diagPath` and said again by `replayDiagnostics` when the crate comes back
// out of the cache. Cargo does the same and for the same reason: a unit's
// fingerprint is over what goes into compiling it - profile, features,
// dependencies, flags - and not over `--message-format`, which decides only
// how what the compiler said is written down. A build that asked for one
// format must not therefore compile the world a second time; Cargo saves
// rustc's diagnostics next to the fingerprint and replays them for a fresh
// unit (`replay_output_cache`, cargo/core/compiler/job_queue).
func (b *Builder) runCompiler(dir string, env map[string]string, pkg *Package, target *Target, diagPath string, args ...string) {
	if b.context.opts.dryRun {
		runCommand(dir, env, "", true, b.context.compiler, args...)

		return
	}

	dumpCommand(env, b.context.compiler, args)

	cmd := exec.Command(b.context.compiler, args...)
	cmd.Dir = dir
	cmd.Env = os.Environ()

	for key, value := range env {
		cmd.Env = append(cmd.Env, key+"="+value)
	}

	cmd.Stdout = io.Discard
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	err := cmd.Run()
	rendered := stderr.String()

	if err != nil {
		if rendered == "" {
			rendered = fmt.Sprintf("error: compiler failed: %v\n", err)
		}

		// Nothing will be cached to replay this from, so it is said here.
		b.sayDiagnostic(pkg, target, "error", rendered)

		throwFmt("compiler failed for %s: %v", targetSourcePath(pkg, target), err)
	}

	throw(os.WriteFile(diagPath, []byte(rendered), 0o644))
}

// Said once per build that needed the crate, whether the crate was compiled
// now or came out of the cache.
func (b *Builder) replayDiagnostics(ctx *TaskContext, unit *CompileUnit) {
	if b.context.opts.dryRun {
		return
	}

	rendered := string(throw2(os.ReadFile(ctx.file(unit.rs, unit.diag))))

	if rendered == "" {
		return
	}

	b.sayDiagnostic(unit.pkg, unit.target, "warning", rendered)
}

// Into the message stream when the build asked for one, onto the standard
// error it inherited when it did not.
func (b *Builder) sayDiagnostic(pkg *Package, target *Target, level string, rendered string) {
	if !b.jsonMessages() {
		cargoMessageMu.Lock()

		defer cargoMessageMu.Unlock()

		_, err := os.Stderr.WriteString(rendered)
		throw(err)

		return
	}

	b.emitDiagnostic(pkg, target, level, rendered)
}

func (b *Builder) reportArtifact(report ArtifactReport) {
	if !b.jsonMessages() {
		return
	}

	var executable any

	if report.executable {
		executable = report.path
	}

	emitCargoMessage(cargoCompilerArtifact{
		Reason:       "compiler-artifact",
		PackageID:    metadataPackageID(report.pkg),
		ManifestPath: report.pkg.manifestPath,
		Target:       messageTarget(report.pkg, report.target),
		Profile:      b.artifactProfile(report.testProfile),
		Features:     activeFeatureNames(report.pkg),
		Filenames:    []string{report.path},
		Executable:   executable,
		Fresh:        report.task.fresh,
	})
}

func reportBuildFinished(opts BuildOptions, success bool) {
	if !jsonMessages(opts) {
		return
	}

	buildFinishedOnce.Do(func() {
		emitCargoMessage(cargoBuildFinished{Reason: "build-finished", Success: success})
	})
}

func emitCargoMessage(message any) {
	data := throw2(json.Marshal(message))
	cargoMessageMu.Lock()

	defer cargoMessageMu.Unlock()

	_, err := os.Stdout.Write(append(data, '\n'))
	throw(err)
}

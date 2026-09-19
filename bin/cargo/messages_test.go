package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Everything the message stream emits goes to standard output, so a test reads
// it by standing in for that file.
func captureMessages(t *testing.T, cb func()) string {
	t.Helper()

	path := filepath.Join(t.TempDir(), "messages.ndjson")
	file, err := os.Create(path)

	if err != nil {
		t.Fatal(err)
	}

	stdout := os.Stdout
	os.Stdout = file

	defer func() {
		os.Stdout = stdout
		_ = file.Close()
	}()

	cb()

	data, err := os.ReadFile(path)

	if err != nil {
		t.Fatal(err)
	}

	return string(data)
}

func decodeMessages(t *testing.T, stream string) []map[string]any {
	t.Helper()

	var messages []map[string]any

	for _, line := range strings.Split(strings.TrimRight(stream, "\n"), "\n") {
		if line == "" {
			continue
		}

		var message map[string]any

		if err := json.Unmarshal([]byte(line), &message); err != nil {
			t.Fatalf("message %q is not JSON: %v", line, err)
		}

		messages = append(messages, message)
	}

	return messages
}

func TestCompilerMessageStartsWithReason(t *testing.T) {
	data, err := json.Marshal(cargoCompilerMessage{
		Reason:       "compiler-message",
		PackageID:    "path+file:///src#test@0.1.0",
		ManifestPath: "/src/Cargo.toml",
		Target:       MetadataTarget{Name: "test", SrcPath: "/src/test.rs"},
		Message:      cargoDiagnostic{Rendered: "error: failed\n", Level: "error"},
	})
	if err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(string(data), `{"reason":"compiler-message"`) {
		t.Fatalf("Cargo message does not start with reason: %s", data)
	}
}

// escargot deserializes a `compiler-message` into shapes whose lists are
// sequences, so a target naming none of its kinds has to say so with an empty
// list rather than with a null.
func TestMessageTargetListsAreNeverNull(t *testing.T) {
	pkg := &Package{dir: "/src", manifestPath: "/src/Cargo.toml", name: "demo"}
	target := &Target{kind: "example", name: "demo", path: "examples/demo.rs", edition: "2021"}
	data := string(throw2(json.Marshal(messageTarget(pkg, target))))

	for _, field := range []string{`"kind":["example"]`, `"crate_types":["bin"]`, `"required-features":[]`} {
		if !strings.Contains(data, field) {
			t.Fatalf("message target %s lacks %s", data, field)
		}
	}
}

// A build script has no target of its own in the manifest, so the stream names
// it the way Cargo does.
func TestBuildScriptMessageTargetIsACustomBuild(t *testing.T) {
	pkg := &Package{
		dir: "/src", manifestPath: "/src/Cargo.toml", name: "demo",
		buildScript: "build.rs", edition: "2021",
	}
	target := &Target{kind: "build-script", name: "build", path: "build.rs"}
	message := messageTarget(pkg, target)

	if message.Name != "build-script-build" || len(message.Kind) != 1 || message.Kind[0] != "custom-build" {
		t.Fatalf("build script target = %+v, want a custom-build named build-script-build", message)
	}
	if message.SrcPath != filepath.Join("/src", "build.rs") {
		t.Fatalf("build script source = %q", message.SrcPath)
	}
}

// trycmd finds the example it is about to run by the `executable` of a
// `compiler-artifact`, and skips whatever a test profile produced.
func TestCompilerArtifactNamesItsExecutable(t *testing.T) {
	pkg := &Package{
		dir: "/src", manifestPath: "/src/Cargo.toml", name: "demo",
		activeFeatures: map[string]bool{"std": true, "derive": true},
	}
	target := &Target{kind: "example", name: "demo", path: "examples/demo.rs", edition: "2021"}
	builder := &Builder{context: &BuildContext{
		opts: BuildOptions{messageFormat: "json", profile: "debug"}, root: pkg,
	}}
	report := ArtifactReport{
		pkg: pkg, target: target, task: &Task{}, path: "/out/demo", executable: true,
	}

	messages := decodeMessages(t, captureMessages(t, func() {
		builder.reportArtifact(report)
	}))

	if len(messages) != 1 {
		t.Fatalf("artifact reported %d messages, want one", len(messages))
	}

	message := messages[0]

	if message["reason"] != "compiler-artifact" {
		t.Fatalf("artifact reason = %v", message["reason"])
	}
	if message["executable"] != "/out/demo" {
		t.Fatalf("artifact executable = %v, want the installed path", message["executable"])
	}
	if profile := message["profile"].(map[string]any); profile["test"] != false {
		t.Fatalf("artifact profile = %v, want a build profile", profile)
	}
	if message["fresh"] != false {
		t.Fatalf("artifact fresh = %v, want the task's own state", message["fresh"])
	}
	if features := message["features"].([]any); len(features) != 2 || features[0] != "derive" {
		t.Fatalf("artifact features = %v, want the package's active features", features)
	}
}

// A consumer reads the stream until `build-finished` tells it the build is
// over; without that line it reads to the end of the pipe instead. The line is
// written once, so what `cargo test` does after the build cannot report a
// second outcome for it.
func TestBuildFinishedReportsTheOutcomeOnce(t *testing.T) {
	opts := BuildOptions{messageFormat: "json"}

	messages := decodeMessages(t, captureMessages(t, func() {
		reportBuildFinished(opts, true)
		reportBuildFinished(opts, false)
		reportBuildFinished(BuildOptions{}, true)
	}))

	if len(messages) != 1 {
		t.Fatalf("build finished reported %d messages, want one", len(messages))
	}
	if messages[0]["reason"] != "build-finished" || messages[0]["success"] != true {
		t.Fatalf("build finished message = %v", messages[0])
	}
}

// A tool's output belongs in the stream, not on an inherited standard error a
// consumer drains only once the build has exited.
func TestToolOutputBecomesACompilerMessage(t *testing.T) {
	pkg := &Package{dir: "/src", manifestPath: "/src/Cargo.toml", name: "demo"}
	target := &Target{kind: "example", name: "demo", path: "examples/demo.rs", edition: "2021"}
	builder := &Builder{context: &BuildContext{
		opts: BuildOptions{messageFormat: "json", profile: "debug"}, root: pkg,
	}}

	stderrPath := filepath.Join(t.TempDir(), "stderr")
	stderrFile := throw2(os.Create(stderrPath))
	stderr := os.Stderr
	os.Stderr = stderrFile

	stream := captureMessages(t, func() {
		builder.runTool("", nil, "", pkg, target, "sh", "-c", "echo to-stdout; echo to-stderr 1>&2")
	})

	os.Stderr = stderr
	throw(stderrFile.Close())

	messages := decodeMessages(t, stream)

	if len(messages) != 1 {
		t.Fatalf("tool output reported %d messages, want one", len(messages))
	}

	message := messages[0]

	if message["reason"] != "compiler-message" {
		t.Fatalf("tool output reason = %v", message["reason"])
	}

	diagnostic := message["message"].(map[string]any)

	if diagnostic["level"] != "warning" {
		t.Fatalf("tool output level = %v, want a warning a consumer ignores", diagnostic["level"])
	}

	rendered := diagnostic["rendered"].(string)

	if !strings.Contains(rendered, "to-stdout") || !strings.Contains(rendered, "to-stderr") {
		t.Fatalf("tool output %q lost a stream", rendered)
	}
	if leaked := string(throw2(os.ReadFile(stderrPath))); leaked != "" {
		t.Fatalf("tool wrote %q to the inherited standard error", leaked)
	}
}

// Without a message stream to write to, a tool keeps the streams it inherited.
func TestToolOutputWithoutAStreamStaysOnStandardError(t *testing.T) {
	pkg := &Package{dir: "/src", manifestPath: "/src/Cargo.toml", name: "demo"}
	target := &Target{kind: "example", name: "demo", path: "examples/demo.rs", edition: "2021"}
	builder := &Builder{context: &BuildContext{opts: BuildOptions{profile: "debug"}, root: pkg}}

	stderrPath := filepath.Join(t.TempDir(), "stderr")
	stderrFile := throw2(os.Create(stderrPath))
	stderr := os.Stderr
	os.Stderr = stderrFile

	stream := captureMessages(t, func() {
		builder.runTool("", nil, "", pkg, target, "sh", "-c", "echo to-stderr 1>&2")
	})

	os.Stderr = stderr
	throw(stderrFile.Close())

	if stream != "" {
		t.Fatalf("a build without a message stream wrote %q to standard output", stream)
	}
	if leaked := string(throw2(os.ReadFile(stderrPath))); !strings.Contains(leaked, "to-stderr") {
		t.Fatalf("tool output %q did not reach standard error", leaked)
	}
}

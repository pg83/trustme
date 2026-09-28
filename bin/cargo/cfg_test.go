package main

import "testing"

func TestCfgExpressions(t *testing.T) {
	cfg := &CfgSet{
		flags: map[string]bool{"unix": true},
		values: map[string]map[string]bool{
			"target_arch": {"x86_64": true},
			"target_os":   {"linux": true},
		},
	}

	features := map[string]bool{"std": true}

	tests := map[string]bool{
		"cfg(unix)":                                         true,
		"cfg(windows)":                                      false,
		"cfg(target_arch = \"x86_64\")":                     true,
		"cfg(all(unix, target_os = \"linux\"))":             true,
		"cfg(any(windows, feature = \"std\"))":              true,
		"cfg(not(any(windows, target_arch = \"aarch64\")))": true,
	}

	for expression, want := range tests {
		if got := cfg.matches(expression, features); got != want {
			t.Errorf("%s = %v, want %v", expression, got, want)
		}
	}
}

// rustix's tests run `cargo tree` with `RUSTFLAGS=--cfg=rustix_use_libc`, which
// selects its libc target table. CARGO_ENCODED_RUSTFLAGS wins when set, even
// empty; with `--target` host units are built without the flags.
func TestRustflagsComeFromTheEnvironment(t *testing.T) {
	t.Setenv("RUSTFLAGS", "--cfg=rustix_use_libc  -C opt-level=1")
	if got := targetRustflags(); len(got) != 3 || got[0] != "--cfg=rustix_use_libc" || got[2] != "opt-level=1" {
		t.Fatalf("RUSTFLAGS = %q", got)
	}
	t.Setenv("CARGO_ENCODED_RUSTFLAGS", "-L\x1fsome dir")
	if got := targetRustflags(); len(got) != 2 || got[1] != "some dir" {
		t.Fatalf("CARGO_ENCODED_RUSTFLAGS = %q", got)
	}
	t.Setenv("CARGO_ENCODED_RUSTFLAGS", "")
	if got := targetRustflags(); len(got) != 0 {
		t.Fatalf("empty CARGO_ENCODED_RUSTFLAGS = %q", got)
	}

	builder := &Builder{context: &BuildContext{rustflags: []string{"--cfg", "x"}, cross: true}}
	if got := builder.unitRustflags(true); len(got) != 0 {
		t.Fatalf("a host unit of a cross build takes %q", got)
	}
	if got := builder.unitRustflags(false); len(got) != 2 {
		t.Fatalf("a target unit takes %q", got)
	}
}

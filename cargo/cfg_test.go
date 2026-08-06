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

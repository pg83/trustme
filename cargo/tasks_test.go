package main

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestDepfileInputs(t *testing.T) {
	path := filepath.Join(t.TempDir(), "crate.d")
	contents := "out.rlib: src/lib.rs src/with\\ space.rs \\\n src/module.rs\n"

	if err := os.WriteFile(path, []byte(contents), 0o644); err != nil {
		t.Fatal(err)
	}

	want := []string{"src/lib.rs", "src/with space.rs", "src/module.rs"}

	if got := depfileInputs(path); !reflect.DeepEqual(got, want) {
		t.Fatalf("depfileInputs() = %#v, want %#v", got, want)
	}
}

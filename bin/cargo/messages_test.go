package main

import (
	"encoding/json"
	"strings"
	"testing"
)

func TestCompilerMessageStartsWithReason(t *testing.T) {
	data, err := json.Marshal(cargoCompilerMessage{
		Reason:  "compiler-message",
		Target:  cargoCompilerMessageTarget{SrcPath: "/src/test.rs"},
		Message: cargoRustcMessage{Rendered: "error: failed\n", Level: "error"},
	})
	if err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(string(data), "{\"reason\":") {
		t.Fatalf("Cargo message does not start with reason: %s", data)
	}
}

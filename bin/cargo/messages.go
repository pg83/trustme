package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strings"
	"sync"
)

var cargoMessageMu sync.Mutex

type cargoCompilerMessage struct {
	Reason  string                     `json:"reason"`
	Target  cargoCompilerMessageTarget `json:"target"`
	Message cargoRustcMessage          `json:"message"`
}

type cargoCompilerMessageTarget struct {
	SrcPath string `json:"src_path"`
}

type cargoRustcMessage struct {
	Rendered string `json:"rendered"`
	Level    string `json:"level"`
}

func (b *Builder) runCompiler(dir string, env map[string]string, srcPath string, args ...string) {
	if !strings.HasPrefix(b.context.opts.messageFormat, "json") || b.context.opts.dryRun {
		runCommand(dir, env, "", b.context.opts.dryRun, b.context.compiler, args...)
		return
	}

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

	if stderr.Len() > 0 || err != nil {
		level := "warning"
		rendered := stderr.String()
		if err != nil {
			level = "error"
			if rendered == "" {
				rendered = fmt.Sprintf("error: compiler failed: %v\n", err)
			}
		}
		emitCargoCompilerMessage(cargoCompilerMessage{
			Reason:  "compiler-message",
			Target:  cargoCompilerMessageTarget{SrcPath: srcPath},
			Message: cargoRustcMessage{Rendered: rendered, Level: level},
		})
	}

	if err != nil {
		throwFmt("compiler failed for %s: %v", srcPath, err)
	}
}

func emitCargoCompilerMessage(message cargoCompilerMessage) {
	data := throw2(json.Marshal(message))
	cargoMessageMu.Lock()
	defer cargoMessageMu.Unlock()
	_, err := os.Stdout.Write(append(data, '\n'))
	throw(err)
}

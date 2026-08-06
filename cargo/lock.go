package main

import (
	"bufio"
	"fmt"
	"os"
	"strings"
)

// pkg is one [[package]] entry from a Cargo.lock.
type pkg struct {
	name     string
	version  string
	source   string // e.g. "registry+https://github.com/rust-lang/crates.io-index"
	checksum string // hex sha256 of the .crate file, empty for path/workspace members
}

// isRegistry reports whether the package is a crates.io registry dependency
// that we can fetch and vendor. Workspace members and path/git deps have no
// registry source and are skipped.
func (p pkg) isRegistry() bool {
	return strings.HasPrefix(p.source, "registry+") && p.checksum != ""
}

// parseLock reads a Cargo.lock and returns its [[package]] entries. The format
// is a tiny, machine-generated subset of TOML (one key = "value" per line
// inside [[package]] tables), so a line scanner is enough — we deliberately
// avoid pulling in a TOML dependency.
func parseLock(path string) ([]pkg, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var pkgs []pkg
	var cur *pkg
	inPackage := false
	flush := func() {
		if cur != nil {
			pkgs = append(pkgs, *cur)
			cur = nil
		}
	}

	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 0, 1<<20), 1<<20)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		switch {
		case line == "[[package]]":
			flush()
			cur = &pkg{}
			inPackage = true
			continue
		case strings.HasPrefix(line, "[") && line != "[[package]]":
			// Some other table (e.g. [[patch.unused]]); leave package scope.
			flush()
			inPackage = false
			continue
		}
		if !inPackage || cur == nil {
			continue
		}
		key, val, ok := splitKV(line)
		if !ok {
			continue
		}
		switch key {
		case "name":
			cur.name = val
		case "version":
			cur.version = val
		case "source":
			cur.source = val
		case "checksum":
			cur.checksum = val
		}
	}
	flush()
	if err := sc.Err(); err != nil {
		return nil, err
	}
	if len(pkgs) == 0 {
		return nil, fmt.Errorf("%s: no [[package]] entries found", path)
	}
	return pkgs, nil
}

// splitKV parses a `key = "value"` line, returning the key and the unquoted
// value. Lines that are not simple string assignments return ok=false.
func splitKV(line string) (key, val string, ok bool) {
	eq := strings.IndexByte(line, '=')
	if eq < 0 {
		return "", "", false
	}
	key = strings.TrimSpace(line[:eq])
	rest := strings.TrimSpace(line[eq+1:])
	if len(rest) < 2 || rest[0] != '"' {
		return "", "", false
	}
	end := strings.IndexByte(rest[1:], '"')
	if end < 0 {
		return "", "", false
	}
	return key, rest[1 : 1+end], true
}

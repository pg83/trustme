package main

import (
	"bufio"
	"os"
	"strings"
)

type Pkg struct {
	name     string
	version  string
	source   string
	checksum string
}

func (p Pkg) isRegistry() bool {
	return strings.HasPrefix(p.source, "registry+") && p.checksum != ""
}

func parseLock(path string) []Pkg {
	f := throw2(os.Open(path))

	defer func() {
		throw(f.Close())
	}()

	var pkgs []Pkg
	var cur *Pkg

	inPackage := false
	inMetadata := false
	metadataChecksums := map[string]string{}

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
			cur = &Pkg{}
			inPackage = true

			continue
		case strings.HasPrefix(line, "[") && line != "[[package]]":

			flush()
			inPackage = false
			inMetadata = line == "[metadata]"

			continue
		}

		if inMetadata {
			if key, sum, ok := parseMetadataChecksum(line); ok {
				metadataChecksums[key] = sum
			}

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
	throw(sc.Err())

	for i := range pkgs {
		if pkgs[i].checksum == "" {
			pkgs[i].checksum = metadataChecksums[pkgs[i].name+" "+pkgs[i].version+" ("+pkgs[i].source+")"]
		}
	}

	if len(pkgs) == 0 {
		throwFmt("%s: no [[package]] entries found", path)
	}

	return pkgs
}

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

func parseMetadataChecksum(line string) (key, sum string, ok bool) {
	const prefix = "\"checksum "

	if !strings.HasPrefix(line, prefix) {
		return "", "", false
	}

	end := strings.IndexByte(line[len(prefix):], '"')

	if end < 0 {
		return "", "", false
	}

	key = line[len(prefix) : len(prefix)+end]

	_, sum, ok = splitKV("k" + line[len(prefix)+end+1:])

	return key, sum, ok
}

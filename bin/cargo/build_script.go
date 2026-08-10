package main

import (
	"bufio"
	"io"
	"os"
	"strings"
)

func loadBuildScriptOutput(pkg *Package, path string) {
	file := throw2(os.Open(path))

	defer func() {
		throw(file.Close())
	}()

	parseBuildScriptOutput(pkg, path, file)
}

func parseBuildScriptOutput(pkg *Package, source string, input io.Reader) {
	output := BuildScriptOutput{env: map[string]string{}, downstream: map[string]string{}}
	scanner := bufio.NewScanner(input)

	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())

		if strings.HasPrefix(line, "cargo::") {
			line = strings.TrimPrefix(line, "cargo::")
		} else if strings.HasPrefix(line, "cargo:") {
			line = strings.TrimPrefix(line, "cargo:")
		} else {
			continue
		}

		key, value, found := strings.Cut(line, "=")

		if !found {
			continue
		}

		switch key {
		case "minicargo-pre-build", "mrustc-pre-build":
			output.preBuild = append(output.preBuild, value)
		case "rustc-link-search":
			_, path, hasKind := strings.Cut(value, "=")

			if !hasKind {
				path = value
			}

			output.linkSearch = append(output.linkSearch, path)
		case "rustc-link-lib":
			kind, name, hasKind := strings.Cut(value, "=")

			if hasKind {
				kind, _, _ = strings.Cut(kind, ":")

				switch kind {
				case "static", "dylib":
					value = name
				case "framework":
					value = "framework=" + name
				default:
					throwFmt("%s: unsupported cargo:rustc-link-lib kind %q", source, kind)
				}
			}

			output.linkLib = append(output.linkLib, value)
		case "rustc-cfg":
			output.cfg = append(output.cfg, value)
		case "rustc-flags":
			output.flags = append(output.flags, strings.Fields(value)...)
		case "rustc-env":
			name, envValue, ok := strings.Cut(value, "=")

			if !ok {
				throwFmt("%s: cargo:rustc-env has no '='", source)
			}

			output.env[name] = envValue
		case "rerun-if-changed", "rerun-if-env-changed":
			output.rerun = append(output.rerun, value)
		default:
			if pkg.links != "" && !strings.Contains(key, "-") {
				name := "DEP_" + cargoEnvName(pkg.links) + "_" + cargoEnvName(key)

				output.downstream[name] = value
			}
		}
	}

	throw(scanner.Err())
	pkg.buildOutput = output
}

func cargoEnvName(value string) string {
	value = strings.ToUpper(value)
	value = strings.ReplaceAll(value, "-", "_")

	return value
}

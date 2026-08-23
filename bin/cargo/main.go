package main

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
)

func main() {
	exc := try(func() {
		dispatch(os.Args)
	})

	exc.catch(func(e *Exception) {
		fmt.Fprintln(os.Stderr, "error:", e.error())
		os.Exit(101)
	})
}

func dispatch(args []string) {
	if len(args) < 2 {
		usage(os.Stderr)
		os.Exit(1)
	}

	commandIndex := 1

	var global []string

parseGlobals:
	for commandIndex < len(args) && strings.HasPrefix(args[commandIndex], "-") {
		arg := args[commandIndex]

		switch arg {
		case "-V", "--version":
			fmt.Println("cargo 0.1.0 (trustme)")

			return
		case "-h", "--help":
			usage(os.Stdout)

			return
		case "-C":
			if commandIndex+1 >= len(args) {
				throwFmt("option -C requires a value")
			}

			commandIndex++
			throw(os.Chdir(args[commandIndex]))
		case "--color", "--config":
			if commandIndex+1 >= len(args) {
				throwFmt("option %s requires a value", arg)
			}

			global = append(global, arg, args[commandIndex+1])
			commandIndex++
		default:
			if name, value, found := strings.Cut(arg, "="); found && (name == "--color" || name == "--config") {
				global = append(global, name, value)
			} else if arg == "-q" || arg == "--quiet" || arg == "-v" || arg == "--verbose" ||
				arg == "--locked" || arg == "--offline" || arg == "--frozen" ||
				len(arg) > 2 && strings.Trim(arg, "v") == "-" {
				global = append(global, arg)
			} else {
				break parseGlobals
			}
		}

		commandIndex++
	}

	if commandIndex >= len(args) {
		throwFmt("no command specified")
	}

	command := args[commandIndex]
	commandArgs := args[commandIndex+1:]

	switch command {
	case "build", "test":
		commandArgs = append(global, commandArgs...)

		opts := parseBuildOptions(command, commandArgs)

		buildProject(opts)

		if opts.pause {
			fmt.Fprintln(os.Stderr, "Press enter to exit...")
			_, _ = bufio.NewReader(os.Stdin).ReadString('\n')
		}
	case "vendor":
		cmdVendor(commandArgs)
	case "help", "-h", "--help":
		usage(os.Stdout)
	default:
		throwFmt("no such command: %s", command)
	}
}

func usage(out *os.File) {
	fmt.Fprint(out, `Rust's package manager (trustme backend)

Usage: cargo [OPTIONS] <COMMAND>

Commands:
  build    Compile the current package
  test     Build and execute tests
  vendor   Vendor all dependencies locally
  help     Print this message

Options:
  -V, --version  Print version info and exit
  -h, --help     Print help
`)
}

func parseBuildOptions(command string, args []string) BuildOptions {
	args = expandOptionEquals(args, map[string]bool{
		"--manifest-path": true, "--target-dir": true, "--target": true,
		"--profile": true, "--jobs": true, "--features": true,
		"--package": true, "--exclude": true, "--bin": true, "--test": true,
		"--example": true, "--bench": true, "--color": true, "--message-format": true,
		"--config": true,
	})

	opts := BuildOptions{
		command:      command,
		manifestPath: "Cargo.toml",
		profile:      "debug",
		jobs:         runtime.NumCPU(),
		targetDir:    os.Getenv("CARGO_TARGET_DIR"),
	}

	for i := 0; i < len(args); i++ {
		arg := args[i]

		if arg == "--" {
			opts.testArgs = append(opts.testArgs, args[i+1:]...)

			break
		}

		value := func() string {
			if i+1 >= len(args) {
				throwFmt("option %s requires a value", arg)
			}

			i++

			return args[i]
		}

		switch arg {
		case "--manifest-path":
			opts.manifestPath = value()
		case "--target-dir":
			opts.targetDir = value()
		case "--target":
			opts.target = value()
		case "--release":
			opts.profile = "release"
		case "--profile":
			opts.profile = value()
		case "-j", "--jobs":
			opts.jobs = parseJobs(value())
		case "--features", "-F":
			opts.features = append(opts.features, splitFeatures(value())...)
		case "--all-features":
			opts.allFeatures = true
		case "--no-default-features":
			opts.noDefault = true
		case "-p", "--package":
			opts.packageName = value()
		case "--workspace", "--all":
			opts.workspaceAll = true
		case "--exclude":
			opts.excludePackages = append(opts.excludePackages, value())
		case "--lib":
			opts.selectors.lib = true
		case "--bins":
			opts.selectors.bins = true
		case "--bin":
			opts.selectors.bin = append(opts.selectors.bin, value())
		case "--tests":
			opts.selectors.tests = true
		case "--test":
			opts.selectors.test = append(opts.selectors.test, value())
		case "--examples":
			opts.selectors.examples = true
		case "--example":
			opts.selectors.example = append(opts.selectors.example, value())
		case "--benches":
			opts.selectors.benches = true
		case "--bench":
			opts.selectors.bench = append(opts.selectors.bench, value())
		case "--no-run":
			opts.noRun = true
		case "-v", "--verbose":
			opts.verbose++
		case "--color", "--message-format", "--config":
			_ = value()
		case "-q", "--quiet", "--locked", "--offline", "--frozen":
		case "-h", "--help":
			buildUsage(command)
			os.Exit(0)
		case "-Z":
			parseUnstable(&opts, value())
		default:
			if strings.HasPrefix(arg, "-j") && len(arg) > 2 {
				opts.jobs = parseJobs(arg[2:])
			} else if len(arg) > 2 && strings.Trim(arg, "v") == "-" {
				opts.verbose += len(arg) - 1
			} else if strings.HasPrefix(arg, "-Z") && len(arg) > 2 {
				parseUnstable(&opts, arg[2:])
			} else {
				throwFmt("unexpected argument %q for cargo %s", arg, command)
			}
		}
	}

	if opts.jobs < 1 {
		throwFmt("jobs must be at least 1")
	}

	if opts.profile == "dev" {
		opts.profile = "debug"
	}

	return opts
}

func parseUnstable(opts *BuildOptions, option string) {
	key, value, hasValue := strings.Cut(option, "=")

	switch key {
	case "vendor-dir":
		requireUnstableValue(key, value, hasValue)
		opts.vendorDir = value
	case "lib-search":
		requireUnstableValue(key, value, hasValue)
		opts.libSearch = append(opts.libSearch, value)
	case "emit-mmir":
		opts.emitMmir = true
	case "dry-run":
		opts.dryRun = true
	case "publish-deps":
		opts.publishDeps = true
	case "pause":
		opts.pause = true
	default:
		throwFmt("unknown unstable option -Z%s", option)
	}
}

func requireUnstableValue(key, value string, present bool) {
	if !present || value == "" {
		throwFmt("unstable option -Z%s requires '=VALUE'", key)
	}
}

func parseJobs(value string) int {
	jobs := throw2(strconv.Atoi(value))

	return jobs
}

func splitFeatures(value string) []string {
	return strings.FieldsFunc(value, func(r rune) bool {
		return r == ',' || r == ' '
	})
}

func expandOptionEquals(args []string, options map[string]bool) []string {
	result := make([]string, 0, len(args))

	for _, arg := range args {
		name, value, found := strings.Cut(arg, "=")

		if found && options[name] {
			result = append(result, name, value)
		} else {
			result = append(result, arg)
		}
	}

	return result
}

func buildUsage(command string) {
	fmt.Printf(`Compile a local package and all of its dependencies

Usage: cargo %s [OPTIONS]

Options:
      --manifest-path <PATH>  Path to Cargo.toml
      --target-dir <DIR>      Directory for generated artifacts
  -j, --jobs <N>              Number of parallel jobs
      --target <TRIPLE>       Build for the target triple
      --release               Build optimized artifacts
  -F, --features <FEATURES>   Features separated by comma or space
      --all-features          Activate all available features
      --no-default-features   Do not activate the default feature
  -p, --package <SPEC>        Build only the specified workspace package
      --workspace             Build all workspace members
      --exclude <SPEC>        Exclude a package from --workspace
      --lib/--bin/--bins      Select library or binary targets
      --test/--tests          Select test targets
      --example/--examples    Select example targets
      --bench/--benches       Select benchmark targets
      --no-run                Build tests without running them

trustme options are passed as -Zname=value: vendor-dir, lib-search;
flags: emit-mmir, dry-run, publish-deps and pause.
`, command)
}

func cmdVendor(args []string) {
	args = expandOptionEquals(args, map[string]bool{"--manifest-path": true})

	manifestPath := "Cargo.toml"
	destination := "vendor"
	archive := ""
	versioned := false
	positional := false

	for i := 0; i < len(args); i++ {
		arg := args[i]

		value := func() string {
			if i+1 >= len(args) {
				throwFmt("option %s requires a value", arg)
			}

			i++

			return args[i]
		}

		switch arg {
		case "--manifest-path":
			manifestPath = value()
		case "--versioned-dirs":
			versioned = true
		case "--locked", "--offline", "--respect-source-config", "--no-delete":
		case "-h", "--help":
			vendorUsage()
			os.Exit(0)
		default:
			if strings.HasPrefix(arg, "-Zarchive=") {
				archive = strings.TrimPrefix(arg, "-Zarchive=")
			} else if strings.HasPrefix(arg, "-") {
				throwFmt("unexpected argument %q for cargo vendor", arg)
			} else if positional {
				throwFmt("cargo vendor accepts only one destination")
			} else {
				destination = arg
				positional = true
			}
		}
	}

	manifestPath = absolutePath(manifestPath)

	lockPath := filepath.Join(filepath.Dir(manifestPath), "Cargo.lock")
	pkgs := parseLock(lockPath)

	if archive != "" {
		work := throw2(os.MkdirTemp("", "cargo-vendor-"))

		defer func() {
			throw(os.RemoveAll(work))
		}()

		vendorDir := filepath.Join(work, "vendor")

		throw(os.MkdirAll(vendorDir, 0o755))
		vendorAll(pkgs, vendorDir, versioned)
		writeConfig(work)
		tarZstd(work, absolutePath(archive))
		fmt.Fprintf(os.Stderr, "Vendored dependencies into %s\n", archive)

		return
	}

	destination = absolutePath(destination)
	throw(os.MkdirAll(destination, 0o755))
	vendorAll(pkgs, destination, versioned)
	fmt.Printf("[source.crates-io]\nreplace-with = \"vendored-sources\"\n\n[source.vendored-sources]\ndirectory = %q\n", destination)
}

func vendorUsage() {
	fmt.Print(`Vendor all dependencies for a project locally

Usage: cargo vendor [OPTIONS] [PATH]

Options:
      --manifest-path <PATH>  Path to Cargo.toml
      --versioned-dirs        Always include versions in directory names
  -Zarchive=<PATH>            Pack the vendor tree as a reproducible tar.zst
`)
}

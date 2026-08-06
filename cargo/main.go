package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
)

func main() {
	exc := try(func() {
		dispatch(os.Args)
	})

	exc.catch(func(e *Exception) {
		fmt.Fprintln(os.Stderr, "cargo:", e.error())
		os.Exit(1)
	})
}

func dispatch(args []string) {
	if len(args) < 2 {
		usage()
		os.Exit(2)
	}

	switch args[1] {
	case "vendor":
		cmdVendor(args[2:])
	case "-h", "--help", "help":
		usage()
	default:
		fmt.Fprintf(os.Stderr, "cargo: unknown command %q\n", args[1])
		usage()
		os.Exit(2)
	}
}

func usage() {
	fmt.Fprint(os.Stderr, `cargo — lockfile-driven cargo for the rustc toolchain

commands:
  vendor   download and pack a project's locked dependencies into a tar.zst

run "cargo vendor -h" for options
`)
}

func cmdVendor(args []string) {
	fs := flag.NewFlagSet("vendor", flag.ExitOnError)
	manifestDir := fs.String("manifest-dir", ".", "directory containing Cargo.lock")
	out := fs.String("out", "vendor.tar.zst", "output .tar.zst archive")
	keep := fs.String("keep-dir", "", "also leave the unpacked vendor tree at this path")

	fs.Parse(args)

	lockPath := filepath.Join(*manifestDir, "Cargo.lock")
	pkgs := parseLock(lockPath)
	work := *keep

	if work == "" {
		work = throw2(os.MkdirTemp("", "cargo-vendor-"))

		defer func() {
			throw(os.RemoveAll(work))
		}()
	} else {
		throw(os.MkdirAll(work, 0o755))
	}

	vendorDir := filepath.Join(work, "vendor")

	throw(os.MkdirAll(vendorDir, 0o755))
	vendorAll(pkgs, vendorDir)
	writeConfig(work)

	outAbs := throw2(filepath.Abs(*out))

	tarZstd(work, outAbs)

	fmt.Fprintf(os.Stderr, "wrote %s\n", *out)
}

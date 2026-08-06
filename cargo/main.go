// Command cargo is a minimal, lockfile-driven cargo replacement for the rustc
// (mrustc-derived) toolchain. It does not resolve dependencies — it trusts a
// committed Cargo.lock — and currently implements one stage: producing a
// hermetic vendored source archive that a downstream build consumes offline.
//
// Usage:
//
//	cargo vendor --manifest-dir DIR --out OUT.tar.zst
package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
)

func main() {
	if len(os.Args) < 2 {
		usage()
		os.Exit(2)
	}
	switch os.Args[1] {
	case "vendor":
		if err := cmdVendor(os.Args[2:]); err != nil {
			fmt.Fprintln(os.Stderr, "cargo vendor:", err)
			os.Exit(1)
		}
	case "-h", "--help", "help":
		usage()
	default:
		fmt.Fprintf(os.Stderr, "cargo: unknown command %q\n", os.Args[1])
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

func cmdVendor(args []string) error {
	fs := flag.NewFlagSet("vendor", flag.ExitOnError)
	manifestDir := fs.String("manifest-dir", ".", "directory containing Cargo.lock")
	out := fs.String("out", "vendor.tar.zst", "output .tar.zst archive")
	keep := fs.String("keep-dir", "", "also leave the unpacked vendor tree at this path")
	fs.Parse(args)

	lockPath := filepath.Join(*manifestDir, "Cargo.lock")
	pkgs, err := parseLock(lockPath)
	if err != nil {
		return err
	}

	work := *keep
	if work == "" {
		work, err = os.MkdirTemp("", "cargo-vendor-")
		if err != nil {
			return err
		}
		defer os.RemoveAll(work)
	} else {
		if err := os.MkdirAll(work, 0o755); err != nil {
			return err
		}
	}

	vendorDir := filepath.Join(work, "vendor")
	if err := os.MkdirAll(vendorDir, 0o755); err != nil {
		return err
	}
	if err := vendorAll(pkgs, vendorDir); err != nil {
		return err
	}
	if err := writeConfig(work); err != nil {
		return err
	}

	outAbs, err := filepath.Abs(*out)
	if err != nil {
		return err
	}
	if err := tarZstd(work, outAbs); err != nil {
		return err
	}
	fmt.Fprintf(os.Stderr, "wrote %s\n", *out)
	return nil
}

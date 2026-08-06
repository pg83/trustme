package main

import (
	"archive/tar"
	"compress/gzip"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

// crateURL returns the crates.io CDN download URL for a package.
func crateURL(p pkg) string {
	return fmt.Sprintf("https://static.crates.io/crates/%s/%s-%s.crate", p.name, p.name, p.version)
}

// vendorLayout decides the on-disk directory name for each package. cargo
// vendor uses the bare crate name when a single version is present and
// name-version when the same crate appears at multiple versions; we match that
// so the output is drop-in for `minicargo --vendor-dir`.
func vendorLayout(pkgs []pkg) map[int]string {
	counts := map[string]int{}
	for _, p := range pkgs {
		if p.isRegistry() {
			counts[p.name]++
		}
	}
	names := map[int]string{}
	for i, p := range pkgs {
		if !p.isRegistry() {
			continue
		}
		if counts[p.name] > 1 {
			names[i] = fmt.Sprintf("%s-%s", p.name, p.version)
		} else {
			names[i] = p.name
		}
	}
	return names
}

// vendorAll downloads, verifies and extracts every registry package from the
// lockfile into vendorDir, writing a cargo-checksum.json alongside each crate.
func vendorAll(pkgs []pkg, vendorDir string) error {
	layout := vendorLayout(pkgs)
	client := &http.Client{Timeout: 120 * time.Second}
	n := 0
	total := len(layout)
	for i, p := range pkgs {
		dir, ok := layout[i]
		if !ok {
			continue
		}
		n++
		dest := filepath.Join(vendorDir, dir)
		fmt.Fprintf(os.Stderr, "vendoring (%d/%d) %s %s\n", n, total, p.name, p.version)
		if err := fetchCrate(client, p, dest); err != nil {
			return fmt.Errorf("%s %s: %w", p.name, p.version, err)
		}
	}
	return nil
}

// fetchCrate downloads one .crate, verifies its sha256 against the lockfile
// checksum, and extracts its contents into dest (stripping the leading
// name-version/ path component the archive is rooted at).
func fetchCrate(client *http.Client, p pkg, dest string) error {
	resp, err := client.Get(crateURL(p))
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("GET %s: %s", crateURL(p), resp.Status)
	}
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return err
	}
	sum := sha256.Sum256(data)
	got := hex.EncodeToString(sum[:])
	if got != p.checksum {
		return fmt.Errorf("checksum mismatch: got %s want %s", got, p.checksum)
	}

	if err := os.RemoveAll(dest); err != nil {
		return err
	}
	if err := os.MkdirAll(dest, 0o755); err != nil {
		return err
	}
	if err := extractCrate(data, dest); err != nil {
		return err
	}

	// cargo verifies each vendored crate against this file. An empty "files"
	// map disables per-file checking; the package sha is the crate checksum.
	meta := map[string]any{"files": map[string]string{}, "package": p.checksum}
	buf, err := json.Marshal(meta)
	if err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(dest, ".cargo-checksum.json"), buf, 0o644)
}

// extractCrate unpacks a gzip'd tar (.crate) into dest, dropping the single
// top-level name-version/ directory the archive wraps everything in.
func extractCrate(data []byte, dest string) error {
	gz, err := gzip.NewReader(strings.NewReader(string(data)))
	if err != nil {
		return err
	}
	defer gz.Close()
	tr := tar.NewReader(gz)
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}
		// Strip the leading path component (name-version/).
		rel := hdr.Name
		if i := strings.IndexByte(rel, '/'); i >= 0 {
			rel = rel[i+1:]
		} else {
			continue // the top directory entry itself
		}
		if rel == "" {
			continue
		}
		target := filepath.Join(dest, filepath.Clean("/"+rel))
		switch hdr.Typeflag {
		case tar.TypeDir:
			if err := os.MkdirAll(target, 0o755); err != nil {
				return err
			}
		case tar.TypeReg:
			if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
				return err
			}
			f, err := os.OpenFile(target, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, os.FileMode(hdr.Mode)&0o777)
			if err != nil {
				return err
			}
			if _, err := io.Copy(f, tr); err != nil {
				f.Close()
				return err
			}
			if err := f.Close(); err != nil {
				return err
			}
		}
	}
	return nil
}

// writeConfig writes the .cargo/config.toml that redirects crates.io to the
// vendored directory, so a downstream build resolves everything offline.
func writeConfig(root string) error {
	dir := filepath.Join(root, ".cargo")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	const cfg = `[source.crates-io]
replace-with = "vendored-sources"

[source.vendored-sources]
directory = "vendor"
`
	return os.WriteFile(filepath.Join(dir, "config.toml"), []byte(cfg), 0o644)
}

// tarZstd packs the given directory tree into out (a .tar.zst), shelling out
// to the system tar which links zstd support. Producing a reproducible archive
// (sorted names, no mtimes/owners) keeps the graph node's output stable.
func tarZstd(root, out string) error {
	cmd := exec.Command("tar",
		"--zstd",
		"--sort=name",
		"--mtime=@0",
		"--owner=0", "--group=0", "--numeric-owner",
		"-cf", out,
		"-C", root,
		".",
	)
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

package main

import (
	"archive/tar"
	"bytes"
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

func crateURL(p Pkg) string {
	return fmt.Sprintf("https://static.crates.io/crates/%s/%s-%s.crate", p.name, p.name, p.version)
}

func vendorLayout(pkgs []Pkg) map[int]string {
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

func vendorAll(pkgs []Pkg, vendorDir string) {
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

		try(func() {
			fetchCrate(client, p, dest)
		}).catch(func(e *Exception) {
			throwFmt("%s %s: %v", p.name, p.version, e.error())
		})
	}
}

func fetchCrate(client *http.Client, p Pkg, dest string) {
	resp := throw2(client.Get(crateURL(p)))

	defer func() {
		throw(resp.Body.Close())
	}()

	if resp.StatusCode != http.StatusOK {
		throwFmt("GET %s: %s", crateURL(p), resp.Status)
	}

	data := throw2(io.ReadAll(resp.Body))
	sum := sha256.Sum256(data)
	got := hex.EncodeToString(sum[:])

	if got != p.checksum {
		throwFmt("checksum mismatch: got %s want %s", got, p.checksum)
	}

	throw(os.RemoveAll(dest))
	throw(os.MkdirAll(dest, 0o755))
	extractCrate(data, dest)

	meta := map[string]any{"files": map[string]string{}, "package": p.checksum}
	buf := throw2(json.Marshal(meta))

	throw(os.WriteFile(filepath.Join(dest, ".cargo-checksum.json"), buf, 0o644))
}

func extractCrate(data []byte, dest string) {
	gz := throw2(gzip.NewReader(bytes.NewReader(data)))

	defer func() {
		throw(gz.Close())
	}()

	tr := tar.NewReader(gz)

	for {
		hdr, err := tr.Next()

		if err == io.EOF {
			break
		}

		throw(err)

		rel := hdr.Name

		if i := strings.IndexByte(rel, '/'); i >= 0 {
			rel = rel[i+1:]
		} else {
			continue
		}

		if rel == "" {
			continue
		}

		target := filepath.Join(dest, filepath.Clean("/"+rel))

		switch hdr.Typeflag {
		case tar.TypeDir:
			throw(os.MkdirAll(target, 0o755))
		case tar.TypeReg:
			extractRegularFile(tr, target, os.FileMode(hdr.Mode)&0o777)
		}
	}
}

func extractRegularFile(tr *tar.Reader, target string, mode os.FileMode) {
	throw(os.MkdirAll(filepath.Dir(target), 0o755))

	f := throw2(os.OpenFile(target, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, mode))

	defer func() {
		throw(f.Close())
	}()

	throw2(io.Copy(f, tr))
}

func writeConfig(root string) {
	dir := filepath.Join(root, ".cargo")

	throw(os.MkdirAll(dir, 0o755))

	const cfg = `[source.crates-io]
replace-with = "vendored-sources"

[source.vendored-sources]
directory = "vendor"
`
	throw(os.WriteFile(filepath.Join(dir, "config.toml"), []byte(cfg), 0o644))
}

func tarZstd(root, out string) {
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
	throw(cmd.Run())
}

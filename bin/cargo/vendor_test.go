package main

import (
	"archive/tar"
	"bytes"
	"io"
	"os"
	"path/filepath"
	"reflect"
	"testing"
	"time"

	"github.com/klauspost/compress/zstd"
)

func TestTarZstdIsSelfContainedAndReproducible(t *testing.T) {
	root := t.TempDir()
	archiveDir := t.TempDir()

	writeArchiveTestFile(t, filepath.Join(root, "z-last"), "last", 0o644)
	writeArchiveTestFile(t, filepath.Join(root, "bin", "tool"), "tool", 0o755)
	writeArchiveTestFile(t, filepath.Join(root, "a-first"), "first", 0o644)

	if err := os.Symlink("a-first", filepath.Join(root, "link")); err != nil {
		t.Fatal(err)
	}

	t.Setenv("PATH", t.TempDir())

	first := filepath.Join(archiveDir, "first.tar.zst")
	second := filepath.Join(archiveDir, "second.tar.zst")
	tarZstd(root, first)

	later := time.Unix(1_800_000_000, 0)

	if err := os.Chtimes(filepath.Join(root, "a-first"), later, later); err != nil {
		t.Fatal(err)
	}

	tarZstd(root, second)

	firstData, err := os.ReadFile(first)

	if err != nil {
		t.Fatal(err)
	}

	secondData, err := os.ReadFile(second)

	if err != nil {
		t.Fatal(err)
	}

	if !bytes.Equal(firstData, secondData) {
		t.Fatal("archive depends on filesystem timestamps")
	}

	entries := readTestArchive(t, firstData)
	want := []testArchiveEntry{
		{name: "a-first", mode: 0o644, body: "first"},
		{name: "bin", mode: 0o755},
		{name: "bin/tool", mode: 0o755, body: "tool"},
		{name: "link", mode: 0o777, link: "a-first"},
		{name: "z-last", mode: 0o644, body: "last"},
	}

	if !reflect.DeepEqual(entries, want) {
		t.Fatalf("archive entries:\n got: %#v\nwant: %#v", entries, want)
	}
}

type testArchiveEntry struct {
	name string
	mode int64
	body string
	link string
}

func writeArchiveTestFile(t *testing.T, path, body string, mode os.FileMode) {
	t.Helper()

	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}

	if err := os.WriteFile(path, []byte(body), mode); err != nil {
		t.Fatal(err)
	}
}

func readTestArchive(t *testing.T, data []byte) []testArchiveEntry {
	t.Helper()

	decoder, err := zstd.NewReader(bytes.NewReader(data))

	if err != nil {
		t.Fatal(err)
	}

	defer decoder.Close()

	reader := tar.NewReader(decoder)
	var entries []testArchiveEntry

	for {
		header, err := reader.Next()

		if err == io.EOF {
			break
		}

		if err != nil {
			t.Fatal(err)
		}

		if header.Uid != 0 || header.Gid != 0 || !header.ModTime.Equal(time.Unix(0, 0)) {
			t.Fatalf("non-reproducible metadata in %q: %#v", header.Name, header)
		}

		body, err := io.ReadAll(reader)

		if err != nil {
			t.Fatal(err)
		}

		entries = append(entries, testArchiveEntry{
			name: header.Name,
			mode: header.Mode,
			body: string(body),
			link: header.Linkname,
		})
	}

	return entries
}

//@ run-pass
// tempfile 3 `create_unix`: `create_helper(dir, .., |path| create_unlinked(&path))` with
// `f: impl FnOnce(PathBuf) -> io::Result<R>`.  Upstream deduces the closure's signature
// from that bound before checking the body (`deduce_closure_signature`), so `path` is a
// `PathBuf` and `&path` reaches `&Path` by dereferencing.  Bound at the closure's end, the
// parameter learnt the closure only after the body had read `path` as `Path` off `&Path`.
use std::fs::File;
use std::io;
use std::path::{Path, PathBuf};

fn create_helper<R>(base: &Path, suffix: &str, f: impl FnOnce(PathBuf) -> io::Result<R>) -> io::Result<R> {
    let path = base.join(format!("tmp{}", suffix));
    f(path)
}

fn create_unlinked(path: &Path) -> io::Result<File> {
    File::create(path)
}

fn create_unix(dir: &Path) -> io::Result<File> {
    create_helper(dir, ".x", |path| create_unlinked(&path))
}

fn main() {
    let dir = std::env::temp_dir();
    let file = create_unix(&dir).unwrap();
    drop(file);
    std::fs::remove_file(dir.join("tmp.x")).unwrap();
}

// Extracted from library/std/src/path.rs:3676
#[cfg(unix)]
fn main() -> std::io::Result<()> {
    use std::path::{self, Path};

    // Relative to absolute
    let absolute = path::absolute("foo/./bar")?;
    assert!(absolute.ends_with("foo/bar"));

    // Absolute to absolute
    let absolute = path::absolute("/foo//test/.././bar.rs")?;
    assert_eq!(absolute, Path::new("/foo/test/../bar.rs"));
    Ok(())
}
#[cfg(not(unix))]
fn main() {}

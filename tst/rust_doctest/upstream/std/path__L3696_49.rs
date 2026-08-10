// Extracted from library/std/src/path.rs:3696
#[cfg(windows)]
fn main() -> std::io::Result<()> {
    use std::path::{self, Path};

    // Relative to absolute
    let absolute = path::absolute("foo/./bar")?;
    assert!(absolute.ends_with(r"foo\bar"));

    // Absolute to absolute
    let absolute = path::absolute(r"C:\foo//test\..\./bar.rs")?;

    assert_eq!(absolute, Path::new(r"C:\foo\bar.rs"));
    Ok(())
}
#[cfg(not(windows))]
fn main() {}

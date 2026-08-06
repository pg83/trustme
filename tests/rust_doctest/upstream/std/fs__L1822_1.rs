// Extracted from library/std/src/fs.rs:1822
Returns the size of the file, in bytes, this metadata is for.

Examples

```no_run
use std::fs;

fn main() -> std::io::Result<()> {
    let metadata = fs::metadata("foo.txt")?;

    assert_eq!(0, metadata.len());
    Ok(())
}

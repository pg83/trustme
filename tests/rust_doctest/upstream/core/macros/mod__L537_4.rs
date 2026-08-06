// Extracted from library/core/src/macros/mod.rs:537
use std::io::Write;

fn main() -> std::io::Result<()> {
    let mut w = Vec::new();
    write!(&mut w, "test")?;
    write!(&mut w, "formatted {}", "arguments")?;

    assert_eq!(w, b"testformatted arguments");
    Ok(())
}

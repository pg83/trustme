// Extracted from library/core/src/macros/mod.rs:625
use std::io::{Write, Result};

fn main() -> Result<()> {
    let mut w = Vec::new();
    writeln!(&mut w)?;
    writeln!(&mut w, "test")?;
    writeln!(&mut w, "formatted {}", "arguments")?;

    assert_eq!(&w[..], "\ntest\nformatted arguments\n".as_bytes());
    Ok(())
}

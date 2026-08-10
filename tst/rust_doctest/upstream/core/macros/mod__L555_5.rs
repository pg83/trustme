// Extracted from library/core/src/macros/mod.rs:555
use std::fmt::Write as _;
use std::io::Write as _;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut s = String::new();
    let mut v = Vec::new();

    write!(&mut s, "{} {}", "abc", 123)?; // uses fmt::Write::write_fmt
    write!(&mut v, "s = {:?}", s)?; // uses io::Write::write_fmt
    assert_eq!(v, b"s = \"abc 123\"");
    Ok(())
}

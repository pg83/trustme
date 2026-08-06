// Extracted from library/core/src/fmt/mod.rs:197
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt::{Error, Write};
        
        fn writer<W: Write>(f: &mut W, s: &str) -> Result<(), Error> {
            f.write_fmt(format_args!("{s}"))
        }
        
        let mut buf = String::new();
        writer(&mut buf, "world")?;
        assert_eq!(&buf, "world");
        std::fmt::Result::Ok(())
        Ok(())
    }
    doctest().unwrap();
}

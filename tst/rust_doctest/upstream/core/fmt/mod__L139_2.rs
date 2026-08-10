// Extracted from library/core/src/fmt/mod.rs:139
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt::{Error, Write};

        fn writer<W: Write>(f: &mut W, s: &str) -> Result<(), Error> {
            f.write_str(s)
        }

        let mut buf = String::new();
        writer(&mut buf, "hola")?;
        assert_eq!(&buf, "hola");
        std::fmt::Result::Ok(())
    }
    doctest().unwrap();
}

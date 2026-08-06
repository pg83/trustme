// Extracted from library/core/src/fmt/mod.rs:167
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt::{Error, Write};
        
        fn writer<W: Write>(f: &mut W, c: char) -> Result<(), Error> {
            f.write_char(c)
        }
        
        let mut buf = String::new();
        writer(&mut buf, 'a')?;
        writer(&mut buf, 'b')?;
        assert_eq!(&buf, "ab");
        std::fmt::Result::Ok(())
        Ok(())
    }
    doctest().unwrap();
}

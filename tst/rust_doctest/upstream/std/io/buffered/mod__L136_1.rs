// Extracted from library/std/src/io/buffered/mod.rs:136
#![allow(unused)]
fn main() {
    use std::io::{BufWriter, ErrorKind, Write};

    let mut not_enough_space = [0u8; 10];
    let mut stream = BufWriter::new(not_enough_space.as_mut());
    write!(stream, "this cannot be actually written").unwrap();
    let into_inner_err = stream.into_inner().expect_err("now we discover it's too small");
    let err = into_inner_err.into_error();
    assert_eq!(err.kind(), ErrorKind::WriteZero);
}

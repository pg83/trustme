// Extracted from library/std/src/io/buffered/bufwriter.rs:164
#![allow(unused)]
fn main() {
    use std::io::{BufWriter, Write};

    let mut buffer = [0u8; 10];
    let mut stream = BufWriter::new(buffer.as_mut());
    write!(stream, "too much data").unwrap();
    stream.flush().expect_err("it doesn't fit");
    let (recovered_writer, buffered_data) = stream.into_parts();
    assert_eq!(recovered_writer.len(), 0);
    assert_eq!(&buffered_data.unwrap(), b"ata");
}

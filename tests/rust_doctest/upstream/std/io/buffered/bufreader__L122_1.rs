// Extracted from library/std/src/io/buffered/bufreader.rs:122
#![allow(unused)]
#![feature(bufreader_peek)]
fn main() {
    use std::io::{Read, BufReader};
    
    let mut bytes = &b"oh, hello there"[..];
    let mut rdr = BufReader::with_capacity(6, &mut bytes);
    assert_eq!(rdr.peek(2).unwrap(), b"oh");
    let mut buf = [0; 4];
    rdr.read(&mut buf[..]).unwrap();
    assert_eq!(&buf, b"oh, ");
    assert_eq!(rdr.peek(5).unwrap(), b"hello");
    let mut s = String::new();
    rdr.read_to_string(&mut s).unwrap();
    assert_eq!(&s, "hello there");
    assert_eq!(rdr.peek(1).unwrap().len(), 0);
}

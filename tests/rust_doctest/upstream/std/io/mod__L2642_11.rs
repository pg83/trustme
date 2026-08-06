// Extracted from library/std/src/io/mod.rs:2642
#![allow(unused)]
fn main() {
    use std::io::{self, BufRead};
    
    let cursor = io::Cursor::new(b"lorem\nipsum\r\ndolor");
    
    let mut lines_iter = cursor.lines().map(|l| l.unwrap());
    assert_eq!(lines_iter.next(), Some(String::from("lorem")));
    assert_eq!(lines_iter.next(), Some(String::from("ipsum")));
    assert_eq!(lines_iter.next(), Some(String::from("dolor")));
    assert_eq!(lines_iter.next(), None);
}

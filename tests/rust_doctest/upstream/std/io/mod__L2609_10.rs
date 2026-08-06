// Extracted from library/std/src/io/mod.rs:2609
#![allow(unused)]
fn main() {
    use std::io::{self, BufRead};
    
    let cursor = io::Cursor::new(b"lorem-ipsum-dolor");
    
    let mut split_iter = cursor.split(b'-').map(|l| l.unwrap());
    assert_eq!(split_iter.next(), Some(b"lorem".to_vec()));
    assert_eq!(split_iter.next(), Some(b"ipsum".to_vec()));
    assert_eq!(split_iter.next(), Some(b"dolor".to_vec()));
    assert_eq!(split_iter.next(), None);
}

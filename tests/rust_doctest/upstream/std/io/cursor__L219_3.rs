// Extracted from library/std/src/io/cursor.rs:219
#![allow(unused)]
#![feature(cursor_split)]
fn main() {
    use std::io::Cursor;
    
    let mut buff = Cursor::new(vec![1, 2, 3, 4, 5]);
    
    assert_eq!(buff.split(), ([].as_slice(), [1, 2, 3, 4, 5].as_slice()));
    
    buff.set_position(2);
    assert_eq!(buff.split(), ([1, 2].as_slice(), [3, 4, 5].as_slice()));
    
    buff.set_position(6);
    assert_eq!(buff.split(), ([1, 2, 3, 4, 5].as_slice(), [].as_slice()));
}

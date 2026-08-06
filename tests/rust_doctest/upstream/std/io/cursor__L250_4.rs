// Extracted from library/std/src/io/cursor.rs:250
#![allow(unused)]
#![feature(cursor_split)]
fn main() {
    use std::io::Cursor;
    
    let mut buff = Cursor::new(vec![1, 2, 3, 4, 5]);
    
    assert_eq!(buff.split_mut(), ([].as_mut_slice(), [1, 2, 3, 4, 5].as_mut_slice()));
    
    buff.set_position(2);
    assert_eq!(buff.split_mut(), ([1, 2].as_mut_slice(), [3, 4, 5].as_mut_slice()));
    
    buff.set_position(6);
    assert_eq!(buff.split_mut(), ([1, 2, 3, 4, 5].as_mut_slice(), [].as_mut_slice()));
}

// Extracted from library/std/src/io/cursor.rs:191
#![allow(unused)]
fn main() {
    use std::io::Cursor;
    
    let mut buff = Cursor::new(vec![1, 2, 3, 4, 5]);
    
    assert_eq!(buff.position(), 0);
    
    buff.set_position(2);
    assert_eq!(buff.position(), 2);
    
    buff.set_position(4);
    assert_eq!(buff.position(), 4);
}

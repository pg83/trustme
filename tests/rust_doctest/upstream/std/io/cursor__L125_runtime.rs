// Extracted from library/std/src/io/cursor.rs:125
#![allow(unused)]
fn main() {
    use std::io::Cursor;
    
    let buff = Cursor::new(Vec::new());
    fn force_inference(_: &Cursor<Vec<u8>>) {}
    force_inference(&buff);
    
    let reference = buff.get_ref();
}

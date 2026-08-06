// Extracted from library/core/src/str/mod.rs:741
#![allow(unused)]
fn main() {
    let s = "Löwe 老虎 Léopard";
    
    unsafe {
        assert_eq!("Löwe 老虎 Léopard", s.slice_unchecked(0, 21));
    }
    
    let s = "Hello, world!";
    
    unsafe {
        assert_eq!("world", s.slice_unchecked(7, 12));
    }
}

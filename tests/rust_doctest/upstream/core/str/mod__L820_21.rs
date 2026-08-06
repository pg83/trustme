// Extracted from library/core/src/str/mod.rs:820
#![allow(unused)]
fn main() {
    let s = "Per Martin-Löf";
    
    let (first, last) = s.split_at(3);
    
    assert_eq!("Per", first);
    assert_eq!(" Martin-Löf", last);
}

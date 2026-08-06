// Extracted from library/core/src/str/mod.rs:399
#![allow(unused)]
#![feature(round_char_boundary)]
fn main() {
    let s = "❤️🧡💛💚💙💜";
    assert_eq!(s.len(), 26);
    assert!(!s.is_char_boundary(13));
    
    let closest = s.floor_char_boundary(13);
    assert_eq!(closest, 10);
    assert_eq!(&s[..closest], "❤️🧡");
}

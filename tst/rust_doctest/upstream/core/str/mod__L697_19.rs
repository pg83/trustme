// Extracted from library/core/src/str/mod.rs:697
#![allow(unused)]
fn main() {
    let mut v = String::from("🗻∈🌏");
    unsafe {
        assert_eq!("🗻", v.get_unchecked_mut(0..4));
        assert_eq!("∈", v.get_unchecked_mut(4..7));
        assert_eq!("🌏", v.get_unchecked_mut(7..11));
    }
}

// Extracted from library/core/src/str/mod.rs:662
#![allow(unused)]
fn main() {
    let v = "🗻∈🌏";
    unsafe {
        assert_eq!("🗻", v.get_unchecked(0..4));
        assert_eq!("∈", v.get_unchecked(4..7));
        assert_eq!("🌏", v.get_unchecked(7..11));
    }
}

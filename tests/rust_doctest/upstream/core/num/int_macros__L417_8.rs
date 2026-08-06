// Extracted from library/core/src/num/int_macros.rs:417
#![allow(unused)]
fn main() {
    if cfg!(target_endian = "little") {
        assert_eq!(n.to_le(), n)
    } else {
        assert_eq!(n.to_le(), n.swap_bytes())
    }
}

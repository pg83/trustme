// Extracted from library/core/src/num/uint_macros.rs:440
#![allow(unused)]
fn main() {
    if cfg!(target_endian = "big") {
        assert_eq!(n.to_be(), n)
    } else {
        assert_eq!(n.to_be(), n.swap_bytes())
    }
}

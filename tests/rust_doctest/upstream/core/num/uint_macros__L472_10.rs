// Extracted from library/core/src/num/uint_macros.rs:472
#![allow(unused)]
fn main() {
    if cfg!(target_endian = "little") {
        assert_eq!(n.to_le(), n)
    } else {
        assert_eq!(n.to_le(), n.swap_bytes())
    }
}

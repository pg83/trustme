// Extracted from library/core/src/num/wrapping.rs:800
#![allow(unused)]
#![feature(wrapping_int_impl)]
fn main() {
    use std::num::Wrapping;
    
    
    
    if cfg!(target_endian = "little") {
        assert_eq!(n.to_le(), n)
    } else {
        assert_eq!(n.to_le(), n.swap_bytes())
    }
}

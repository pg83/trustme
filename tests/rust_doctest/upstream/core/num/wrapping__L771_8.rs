// Extracted from library/core/src/num/wrapping.rs:771
#![allow(unused)]
#![feature(wrapping_int_impl)]
fn main() {
    use std::num::Wrapping;
    
    
    
    if cfg!(target_endian = "big") {
        assert_eq!(n.to_be(), n)
    } else {
        assert_eq!(n.to_be(), n.swap_bytes())
    }
}

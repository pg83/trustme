// Extracted from library/core/src/num/saturating.rs:746
#![allow(unused)]
fn main() {
    use std::num::Saturating;
    
    
    
    if cfg!(target_endian = "big") {
        assert_eq!(n.to_be(), n)
    } else {
        assert_eq!(n.to_be(), n.swap_bytes())
    }
}

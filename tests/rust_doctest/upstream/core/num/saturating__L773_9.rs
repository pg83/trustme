// Extracted from library/core/src/num/saturating.rs:773
#![allow(unused)]
fn main() {
    use std::num::Saturating;
    
    
    
    if cfg!(target_endian = "little") {
        assert_eq!(n.to_le(), n)
    } else {
        assert_eq!(n.to_le(), n.swap_bytes())
    }
}

// Extracted from library/core/src/fmt/mod.rs:1177
#![allow(unused)]
fn main() {
    use std::fmt;
    
    struct Length(i32);
    
    impl fmt::LowerHex for Length {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            let val = self.0;
    
            fmt::LowerHex::fmt(&val, f) // delegate to i32's implementation
        }
    }
    
    let l = Length(9);
    
    assert_eq!(format!("l as hex is: {l:x}"), "l as hex is: 9");
    
    assert_eq!(format!("l as hex is: {l:#010x}"), "l as hex is: 0x00000009");
}

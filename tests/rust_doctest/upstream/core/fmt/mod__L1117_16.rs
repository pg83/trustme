// Extracted from library/core/src/fmt/mod.rs:1117
#![allow(unused)]
fn main() {
    use std::fmt;
    
    struct Length(i32);
    
    impl fmt::Binary for Length {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            let val = self.0;
    
            fmt::Binary::fmt(&val, f) // delegate to i32's implementation
        }
    }
    
    let l = Length(107);
    
    assert_eq!(format!("l as binary is: {l:b}"), "l as binary is: 1101011");
    
    assert_eq!(
        // Note that the `0b` prefix added by `#` is included in the total width, so we
        // need to add two to correctly display all 32 bits.
        format!("l as binary is: {l:#034b}"),
        "l as binary is: 0b00000000000000000000000001101011"
    );
}

// Extracted from library/core/src/fmt/mod.rs:1232
#![allow(unused)]
fn main() {
    use std::fmt;

    struct Length(i32);

    impl fmt::UpperHex for Length {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            let val = self.0;

            fmt::UpperHex::fmt(&val, f) // delegate to i32's implementation
        }
    }

    let l = Length(i32::MAX);

    assert_eq!(format!("l as hex is: {l:X}"), "l as hex is: 7FFFFFFF");

    assert_eq!(format!("l as hex is: {l:#010X}"), "l as hex is: 0x7FFFFFFF");
}

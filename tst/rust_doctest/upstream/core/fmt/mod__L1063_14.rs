// Extracted from library/core/src/fmt/mod.rs:1063
#![allow(unused)]
fn main() {
    use std::fmt;

    struct Length(i32);

    impl fmt::Octal for Length {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            let val = self.0;

            fmt::Octal::fmt(&val, f) // delegate to i32's implementation
        }
    }

    let l = Length(9);

    assert_eq!(format!("l as octal is: {l:o}"), "l as octal is: 11");

    assert_eq!(format!("l as octal is: {l:#06o}"), "l as octal is: 0o0011");
}

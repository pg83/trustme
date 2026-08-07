// Extracted from library/core/src/fmt/mod.rs:1388
#![allow(unused)]
fn main() {
    use std::fmt;

    struct Length(i32);

    impl fmt::UpperExp for Length {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            let val = f64::from(self.0);
            fmt::UpperExp::fmt(&val, f) // delegate to f64's implementation
        }
    }

    let l = Length(100);

    assert_eq!(
        format!("l in scientific notation is: {l:E}"),
        "l in scientific notation is: 1E2"
    );

    assert_eq!(
        format!("l in scientific notation is: {l:05E}"),
        "l in scientific notation is: 001E2"
    );
}

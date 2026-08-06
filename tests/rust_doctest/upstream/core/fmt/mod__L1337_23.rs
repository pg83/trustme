// Extracted from library/core/src/fmt/mod.rs:1337
#![allow(unused)]
fn main() {
    use std::fmt;
    
    struct Length(i32);
    
    impl fmt::LowerExp for Length {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            let val = f64::from(self.0);
            fmt::LowerExp::fmt(&val, f) // delegate to f64's implementation
        }
    }
    
    let l = Length(100);
    
    assert_eq!(
        format!("l in scientific notation is: {l:e}"),
        "l in scientific notation is: 1e2"
    );
    
    assert_eq!(
        format!("l in scientific notation is: {l:05e}"),
        "l in scientific notation is: 001e2"
    );
}

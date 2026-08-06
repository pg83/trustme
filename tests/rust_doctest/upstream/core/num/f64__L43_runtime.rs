// Extracted from library/core/src/num/f64.rs:43
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let d = std::f64::MANTISSA_DIGITS;
    
    // intended way
    let d = f64::MANTISSA_DIGITS;
}

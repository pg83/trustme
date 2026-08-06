// Extracted from library/core/src/num/f32.rs:43
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let d = std::f32::MANTISSA_DIGITS;
    
    // intended way
    let d = f32::MANTISSA_DIGITS;
}

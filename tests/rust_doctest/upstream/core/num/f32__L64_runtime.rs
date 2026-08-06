// Extracted from library/core/src/num/f32.rs:64
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let d = std::f32::DIGITS;
    
    // intended way
    let d = f32::DIGITS;
}

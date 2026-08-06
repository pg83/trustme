// Extracted from library/core/src/num/f32.rs:122
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let min = std::f32::MIN_POSITIVE;
    
    // intended way
    let min = f32::MIN_POSITIVE;
}

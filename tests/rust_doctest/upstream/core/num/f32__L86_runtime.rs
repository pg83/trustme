// Extracted from library/core/src/num/f32.rs:86
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let e = std::f32::EPSILON;
    
    // intended way
    let e = f32::EPSILON;
}

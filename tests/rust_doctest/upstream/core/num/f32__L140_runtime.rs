// Extracted from library/core/src/num/f32.rs:140
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let max = std::f32::MAX;
    
    // intended way
    let max = f32::MAX;
}

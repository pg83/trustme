// Extracted from library/core/src/num/f32.rs:248
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let inf = std::f32::INFINITY;
    
    // intended way
    let inf = f32::INFINITY;
}

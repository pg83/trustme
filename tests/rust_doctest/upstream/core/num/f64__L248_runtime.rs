// Extracted from library/core/src/num/f64.rs:248
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let inf = std::f64::INFINITY;
    
    // intended way
    let inf = f64::INFINITY;
}

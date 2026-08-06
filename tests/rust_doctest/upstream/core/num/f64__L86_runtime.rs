// Extracted from library/core/src/num/f64.rs:86
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let e = std::f64::EPSILON;
    
    // intended way
    let e = f64::EPSILON;
}

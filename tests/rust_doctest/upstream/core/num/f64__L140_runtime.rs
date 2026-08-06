// Extracted from library/core/src/num/f64.rs:140
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let max = std::f64::MAX;
    
    // intended way
    let max = f64::MAX;
}

// Extracted from library/core/src/num/f32.rs:230
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let nan = std::f32::NAN;
    
    // intended way
    let nan = f32::NAN;
}

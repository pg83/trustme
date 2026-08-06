// Extracted from library/std/src/num/f32.rs:477
#![allow(unused)]
fn main() {
    let five = 5.0f32;
    
    // log5(5) - 1 == 0
    let abs_difference = (five.log(5.0) - 1.0).abs();
    
    assert!(abs_difference <= 1e-6);
}

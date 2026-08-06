// Extracted from library/core/src/hint.rs:456
#![allow(unused)]
fn main() {
    use std::hint::black_box;
    
    // The compiler sees this...
    let y = black_box(5 * 10);
    
    // ...as this. As such, it will likely simplify `5 * 10` to just `50`.
    let _0 = 5 * 10;
    let y = black_box(_0);
}

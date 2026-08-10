// Extracted from library/core/src/ops/function.rs:118
#![allow(unused)]
fn main() {
    let mut x = 5;
    {
        let mut square_x = || x *= x;
        square_x();
    }
    assert_eq!(x, 25);
}

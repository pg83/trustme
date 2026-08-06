// Extracted from src/expressions/operator-expr.md:1184
#![allow(unused)]
fn main() {
    use std::ops::AddAssign;
    fn f<T: AddAssign + Copy>(mut x: T, y: T) {
        x += y; // Statement 1.
        x.add_assign(y); // Statement 2.
    }
}

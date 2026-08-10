// Extracted from library/std/src/macros.rs:290
#![allow(unused)]
fn main() {
    fn factorial(n: u32) -> u32 {
        if dbg!(n <= 1) {
            dbg!(1)
        } else {
            dbg!(n * factorial(n - 1))
        }
    }

    dbg!(factorial(4));
}

// Extracted from src/types/closure.md:614
#![allow(unused)]
fn main() {
    let mut b = false;
    let x = &mut b;
    let mut c = || {
        // An ImmBorrow and a MutBorrow of `x`.
        let a = &x;
        *x = true; // `x` captured by UniqueImmBorrow
    };
    // The following line is an error:
    // let y = &x;
    c();
    // However, the following is OK.
    let z = &x;
}

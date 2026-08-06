// Extracted from src/types/closure.md:294
#![allow(unused)]
fn main() {
    struct S; // A non-`Copy` type.
    let mut x = (Some(S), S);
    let c = || match x {
        (None, _) => (),
    //   ^^^^
    // This pattern requires reading the discriminant, which
    // causes `x.0` to be captured by `ImmBorrow`.
        _ => (),
    };
    let _ = &mut x.0; // ERROR: Cannot borrow `x.0` as mutable.
    //           ^^^
    // The closure is still live, so `x.0` is still immutably
    // borrowed here.
    c();
}

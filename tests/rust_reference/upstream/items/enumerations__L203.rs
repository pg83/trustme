// Extracted from src/items/enumerations.md:203
#![allow(unused)]
fn main() {
    #[repr(u32)]
    enum E<'a, T, const N: u32> {
        Lifetime(&'a T) = {
            let a: &'a (); // ERROR.
            1
        },
        Type(T) = {
            let x: T; // ERROR.
            2
        },
        Const = N, // ERROR.
    }
}

//@ compile-fail: A mutable reference is not allowed in the final value of a constant
// What a `const` holds is the value of its body's tail, and that value is kept
// for the life of the program, so a mutable reference may not be part of it.
// Coercing `&mut 0` to `&u8` does not change what the place behind it is.

const C: &u8 = &mut 0;

fn main() {
    let _ = C;
}

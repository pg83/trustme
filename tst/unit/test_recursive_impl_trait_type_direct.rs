//@ crate-type: lib

#![allow(unconditional_recursion)]

fn recursive() -> impl Sized {
    recursive()
}

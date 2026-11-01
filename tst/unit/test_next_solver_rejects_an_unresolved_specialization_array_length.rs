//@ compile-fail: Failed to infer const argument
//@ compile-flags: -Znext-solver=globally
#![feature(specialization, generic_arg_infer)]
#![allow(incomplete_features)]

trait Pick { fn code() -> u8; }
impl<A, B> Pick for (A, B) { default fn code() -> u8 { 1 } }
impl<T> Pick for (T, T) { fn code() -> u8 { 2 } }

fn main() {
    let _ = <([u8; _], [u8; 2]) as Pick>::code();
}

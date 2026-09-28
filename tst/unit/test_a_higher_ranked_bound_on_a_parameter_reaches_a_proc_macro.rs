//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// yoke-derive's example derives on `struct .. <T: for<'a> Trait<'a>>`: a bound
// written on the parameter itself with its own `for<..>` binder. The item is
// handed to the macro as its tokens, the binder included, the same as a
// where-clause bound already was; the parameter case was a TODO that aborted
// the compiler ("visitParams - be.inner_hrbs").
use proc_macro_item_passthrough::echo_item;

#[echo_item]
struct Holder<F: for<'a> Fn(&'a u8) -> u8> {
    f: F,
}

fn main() {
    let holder = Holder { f: |x: &u8| *x + 1 };
    assert_eq!((holder.f)(&1), 2);
}

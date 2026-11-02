//@ compile-variant: -O
#![feature(stmt_expr_attributes)]
macro_rules! hidden_tail { () => { #[cfg(any())] 99u8 }; }
fn empty() { hidden_tail!() }
fn main() {
    #[cfg(any())]
    absent_macro!();
    #[cfg(any())]
    { absent_function(); }
    empty();
    assert_eq!({ hidden_tail!() }, ());
}

//@ proc-macro-aux-build: empty_block_macro_control.rs
//@ compile-variant: -O

fn empty_tail() { empty_block_macro_control::nothing!() }

fn main() {
    assert_eq!(empty_tail(), ());
}

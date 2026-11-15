//@ proc-macro-aux-build: cov99_w1669_assert_fragment_aux.rs
//@ compile-only
//@ compile-flags: --emit=metadata

#[macro_use]
extern crate cov99_w1669_assert_fragment_aux;

fn main() {
    assert!(true, "{}", 7u8);
}

//@ edition: 2024
//@ proc-macro-aux-build: emits_ref_binding_under_default_borrow.rs
// Edition 2024 rejects `ref` on a binding inside a pattern that already borrows
// implicitly, but rustc decides that by the edition of the pattern's span. The
// pattern here comes from a 2018 macro, where `ref` is still allowed.
use emits_ref_binding_under_default_borrow::first_len;

first_len!();

fn main() {
    assert_eq!(first_len(&Some(String::from("abc"))), 3);
    assert_eq!(first_len(&None), 0);
}

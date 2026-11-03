//@ compile-fail: Unknown start of token
//@ proc-macro-aux-build: doc_token_observer.rs
use doc_token_observer::discard;

fn main() {
    assert_eq!(discard!(`), 73);
}

//@ test-harness
//@ edition: 2021
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// tokio's tests write `use tokio::test as maybe_tokio_test;` and
// `#[maybe_tokio_test] async fn ..`. The attribute is the proc macro the
// import names; the macro's own name being `test` does not make it the
// built-in `#[test]`, which would register the async fn itself as a test
// (`Termination` for its future).

use proc_macro_item_passthrough::test as maybe_async_test;

#[maybe_async_test]
async fn becomes_a_plain_test() {
    panic!("the attribute drops the body");
}

// `use tokio::test; #[test] async fn ..` (tokio's macros_test): an import
// shadows the prelude, where the built-in `test` lives.
mod imported {
    use proc_macro_item_passthrough::test;

    #[test]
    async fn also_becomes_a_plain_test() {
        panic!("the attribute drops the body");
    }
}

// The crate's own `test` attribute is what `named_test_macros::test` names, not
// the builtin `#[test]` the prelude brings into every module (tokio's
// `pub use tokio_macros::test`).
mod exported {
    pub use named_test_macros::main;
    pub use named_test_macros::test;
}

#[exported::test]
fn tagged() -> u32 {
    7
}

#[exported::main]
fn main() {
    assert_eq!(tagged(), 7);
}

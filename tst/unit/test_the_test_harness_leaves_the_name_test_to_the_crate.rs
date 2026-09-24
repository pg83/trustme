//@ test-harness
//@ edition: 2021
// tokio's macros_rename_test has `mod test { pub use ::tokio; }` and a
// `#[tokio::test]` whose expansion says `use test::tokio::runtime::Builder;`.
// Upstream's harness injects `extern crate test` under a hygienic name, so
// the crate's own `test` is the only one its code sees; ours put `test` in
// the extern prelude, which a 2018 import consults first.

mod test {
    pub fn seven() -> u8 {
        7
    }
}

#[test]
fn a_use_names_the_module() {
    use test::seven;
    assert_eq!(seven(), 7);
}

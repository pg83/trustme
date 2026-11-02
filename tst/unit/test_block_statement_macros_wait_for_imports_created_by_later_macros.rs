//@ proc-macro-aux-build: statement_macro_imports.rs
//@ compile-variant: -O
macro_rules! install { () => { use statement_macro_imports::make_value as deferred; }; }
fn main() {
    deferred!();
    install!();
    assert_eq!(generated, 17);
}

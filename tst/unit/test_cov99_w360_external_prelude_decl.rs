//@ aux-build: cov99_w360_prelude_decl.rs

fn main() {
    assert_eq!(cov99_w360_prelude_decl::prelude_text!(), "w360");
}

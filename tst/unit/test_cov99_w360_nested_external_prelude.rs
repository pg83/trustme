//@ aux-build: cov99_w360_nested_prelude.rs

fn main() {
    assert_eq!(cov99_w360_nested_prelude::definition::prelude_text!(), "w360");
}

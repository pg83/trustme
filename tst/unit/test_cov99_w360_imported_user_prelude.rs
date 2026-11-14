//@ aux-build: cov99_w360_user_prelude.rs

#[macro_use]
extern crate cov99_w360_user_prelude;

fn main() {
    assert_eq!(cov99_w360_user_prelude::definition::from_prelude!(), 360u16);
}

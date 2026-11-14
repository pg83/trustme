//@ aux-build: cov99_w359_private_macro.rs

#[macro_use]
extern crate cov99_w359_private_macro;
use cov99_w359_private_macro::w359_public_choose as w359_choose;

fn main() {
    macro_rules! insert_function {
        () => { fn chosen() -> u8 { w359_choose!() } };
    }
    insert_function!();
    assert_eq!(chosen(), 42u8);
}

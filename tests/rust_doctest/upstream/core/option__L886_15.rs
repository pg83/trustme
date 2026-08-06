// Extracted from library/core/src/option.rs:886
#![allow(unused)]
fn main() {
    assert_eq!(Some(123).as_mut_slice().first_mut(), Some(&mut 123))
}

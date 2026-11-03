//@ reject-flags: --cfg bad_type => auto trait || auto traits cannot have generic parameters
//@ reject-flags: --cfg bad_lifetime => auto trait || auto traits cannot have generic parameters
//@ reject-flags: --cfg bad_const => auto trait || auto traits cannot have generic parameters
#![feature(auto_traits)]
#[cfg(bad_type)]
auto trait Invalid<T> {}
#[cfg(bad_lifetime)]
auto trait Invalid<'a> {}
#[cfg(bad_const)]
auto trait Invalid<const N: usize> {}
auto trait Plain {}
fn require<T: Plain>(value: T) -> T { value }
fn main() { assert_eq!(require(227u16), 227); }

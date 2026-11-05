//@ compile-fail: error:
#![feature(auto_traits, negative_impls)]

auto trait Portable {}
struct Local;
impl !Portable for Local {}
fn require<T: Portable>() {}

fn main() { require::<(u8, Local, u16)>(); }

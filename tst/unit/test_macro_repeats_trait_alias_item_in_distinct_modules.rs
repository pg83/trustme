#![feature(trait_alias)]
macro_rules! duplicate { ($item:item) => { mod left { $item } mod right { $item } }; }
duplicate! { pub trait Share = Copy + std::fmt::Debug; }
fn pair<T: left::Share + right::Share>(value:T)->(T,T) { (value,value) }
fn main() { assert_eq!(pair(257u16),(257,257));assert_eq!(pair("repeat"),("repeat","repeat")); }

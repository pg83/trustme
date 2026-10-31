#![feature(trait_alias)]
macro_rules! item { ($item:item) => { $item }; }
item! { trait Share = Copy + std::fmt::Debug; }
fn duplicate<T: Share>(value:T)->(T,T) { (value,value) }
fn main() { assert_eq!(duplicate(37u16),(37,37));assert_eq!(duplicate("alias"),("alias","alias")); }

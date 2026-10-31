#![feature(deref_patterns)]
fn main() { let deref!((first, last)): Box<(u8,u16)>; first=19; last=257; assert_eq!((first,last),(19,257)); }

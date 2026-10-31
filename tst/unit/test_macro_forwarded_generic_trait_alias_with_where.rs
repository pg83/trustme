#![feature(trait_alias)]
macro_rules! item { ($item:item) => { $item }; }
item! { trait Bytes<T> = Iterator<Item=T> where T: Copy; }
fn total<I: Bytes<u8>>(iter: I) -> u32 { iter.map(|n| n as u32).sum() }
fn main() { assert_eq!(total([3u8,5,7].into_iter()),15); }

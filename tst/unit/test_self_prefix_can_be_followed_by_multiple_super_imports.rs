//@ compile-variant: -O
const VALUE: u32 = 41;
mod outer { pub mod inner { use self::super::super::VALUE; pub fn read() -> u32 { VALUE } } }
fn main() { assert_eq!(outer::inner::read(), 41); }

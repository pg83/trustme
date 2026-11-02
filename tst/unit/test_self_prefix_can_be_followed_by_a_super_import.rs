//@ compile-variant: -O
const VALUE: u32 = 37;
mod nested { use self::super::VALUE; pub fn read() -> u32 { VALUE } }
fn main() { assert_eq!(nested::read(), 37); }

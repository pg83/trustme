//@ compile-fail: macro_use
#[macro_use(only_value)]
mod definitions { macro_rules! only_value { () => { 179u16 }; } }
fn main() { assert_eq!(only_value!(), 179); }

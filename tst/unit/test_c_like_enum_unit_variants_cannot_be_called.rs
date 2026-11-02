//@ compile-fail: Unable to find an implementation of Fn*
enum Flag { Off, On }
fn main() { let _ = Flag::On(); }

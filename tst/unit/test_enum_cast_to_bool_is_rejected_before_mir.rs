//@ compile-fail: Invalid cast from enum
enum Flag { Set = 1 }
fn main() { let value = Flag::Set; let _: bool = value as bool; }

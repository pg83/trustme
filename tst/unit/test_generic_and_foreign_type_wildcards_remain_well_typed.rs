//@ crate-type: lib
#![feature(extern_types)]
unsafe extern "C" { pub type Foreign; }
pub fn foreign(value: &Foreign, flag: bool) -> u8 {
    match (flag, value) { (true, _) => 17, (false, _) => 23 }
}
pub fn array<const N: usize>(values: [u8; N], flag: bool) -> u8 {
    match (flag, values) { (true, _) => 29, (false, _) => 31 }
}
pub fn generic<T>(value: T, flag: bool) -> u8 {
    match (flag, value) { (true, _) => 37, (false, _) => 41 }
}

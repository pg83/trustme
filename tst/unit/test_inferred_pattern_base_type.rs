#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
fn main() {
    let value = 19u8;
    let input: pattern_type!(*const u8 is !null) = unsafe { std::mem::transmute(&value as *const u8) };
    let pointer: pattern_type!(*const _ is !null) = input;
    let plain: *const u8 = unsafe { std::mem::transmute(pointer) };
    assert_eq!(unsafe { *plain }, 19);
}

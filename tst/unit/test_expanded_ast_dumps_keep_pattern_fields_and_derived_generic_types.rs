//@ compile-flags: -Zdump-ast
#![feature(pattern_types, pattern_type_macro)]
#[derive(Clone, PartialEq, Eq)]
struct Record<T> { value: T, bounded: std::pat::pattern_type!(u16 is 3..=9) }
struct Present(std::pat::pattern_type!(*const u32 is !null));
fn main() {
    let original = Record { value: 419u32, bounded: 7 };
    assert!(original.clone() == original);
    let value = 421u32;
    let present = Present(unsafe { std::mem::transmute(&value as *const u32) });
    let pointer: *const u32 = unsafe { std::mem::transmute(present.0) };
    assert_eq!(unsafe { *pointer }, 421);
}

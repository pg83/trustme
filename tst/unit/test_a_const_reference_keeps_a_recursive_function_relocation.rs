//@ compile-fail: error
#![feature(adt_const_params, unsized_const_params)]
#![allow(incomplete_features)]
#[repr(C)]
struct Data { callback: fn() -> u8, byte: u8 }
static DATA: Data = Data { callback: read::<{ &DATA.byte }>, byte: 23 };
#[inline(never)]
fn read<const VALUE: &'static u8>() -> u8 { *VALUE }
fn main() {
    assert_eq!(read::<{ &DATA.byte }>(), 23);
    assert_eq!((DATA.callback)(), 23);
}

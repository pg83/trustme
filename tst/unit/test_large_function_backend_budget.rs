//@ compile-flags: -O

use std::hint::black_box;

#[inline(never)]
fn work(mut value: u64) -> u64 {
    value = black_box(value.wrapping_add(1));
    value = black_box(value.wrapping_add(1));
    value = black_box(value.wrapping_add(1));
    value = black_box(value.wrapping_add(1));
    value = black_box(value.wrapping_add(1));
    value = black_box(value.wrapping_add(1));
    value = black_box(value.wrapping_add(1));
    value = black_box(value.wrapping_add(1));
    value = black_box(value.wrapping_add(1));
    value = black_box(value.wrapping_add(1));
    value = black_box(value.wrapping_add(1));
    value
}

macro_rules! call_work {
    ($value:ident) => {
        $value = work($value);
    };
}

macro_rules! call_2 { ($value:ident) => { call_work!($value); call_work!($value); }; }
macro_rules! call_4 { ($value:ident) => { call_2!($value); call_2!($value); }; }
macro_rules! call_8 { ($value:ident) => { call_4!($value); call_4!($value); }; }
macro_rules! call_16 { ($value:ident) => { call_8!($value); call_8!($value); }; }
macro_rules! call_32 { ($value:ident) => { call_16!($value); call_16!($value); }; }
macro_rules! call_64 { ($value:ident) => { call_32!($value); call_32!($value); }; }
macro_rules! call_128 { ($value:ident) => { call_64!($value); call_64!($value); }; }
macro_rules! call_256 { ($value:ident) => { call_128!($value); call_128!($value); }; }
macro_rules! call_512 { ($value:ident) => { call_256!($value); call_256!($value); }; }
macro_rules! call_1024 { ($value:ident) => { call_512!($value); call_512!($value); }; }
macro_rules! call_2048 { ($value:ident) => { call_1024!($value); call_1024!($value); }; }
macro_rules! call_4096 { ($value:ident) => { call_2048!($value); call_2048!($value); }; }
macro_rules! call_8192 { ($value:ident) => { call_4096!($value); call_4096!($value); }; }
macro_rules! call_16384 { ($value:ident) => { call_8192!($value); call_8192!($value); }; }

#[no_mangle]
pub fn trustme_large_backend_budget() -> u64 {
    let guard = String::from("kept live across every call");
    let mut value = 0;
    call_16384!(value);
    black_box(guard);
    value
}

fn main() {
    assert_eq!(trustme_large_backend_budget(), 16_384 * 11);
}

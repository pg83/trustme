// rustsmith's seed 7 is one function of thousands of calls to wrappers the
// MIR inliner takes. Each inline appended its blocks after an exact
// `reserve`, moving every block of the caller, and checked recursion against
// every earlier inline; the compile took 294 s. rustc's inliner extends the
// caller's blocks with amortised growth and keeps the chain of inlined
// callees a block came from (`history` in rustc_mir_transform's inliner).
//@ crate-type: lib
//@ compile-flags: -O -C emit-cpp-only

use std::hint::black_box;

#[inline(never)]
fn work(value: u64) -> u64 {
    black_box(value.wrapping_add(1))
}

fn step(value: u64) -> u64 {
    work(value)
}

macro_rules! call_step {
    ($value:ident) => {
        $value = step($value);
    };
}

macro_rules! call_2 { ($value:ident) => { call_step!($value); call_step!($value); }; }
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
macro_rules! call_32768 { ($value:ident) => { call_16384!($value); call_16384!($value); }; }

pub fn walk(mut value: u64) -> u64 {
    call_32768!(value);
    value
}

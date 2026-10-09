#![feature(trivial_clone)]
// core 1.93's `TrivialClone` gets rustc's builtin copy/clone candidates
// (`assemble_builtin_copy_clone_candidate`: tuples, closures, function
// pointers and items, each constituent needing the trait), and
// `#[derive(Clone)]` emits `unsafe impl TrivialClone` when the clone is a
// plain copy: a union, or a type that also derives `Copy` and has no type
// parameter. We had neither.

use std::clone::TrivialClone;

#[derive(Clone, Copy, Debug, PartialEq)]
struct Point {
    x: i32,
    y: i32,
}

#[derive(Clone, Copy)]
union Bits {
    word: u32,
    bytes: [u8; 4],
}

fn trivially<T: TrivialClone>(value: &T) -> T {
    value.clone()
}

fn main() {
    assert_eq!(trivially(&(1u8, 2u16)), (1, 2));
    let f: fn() -> u8 = || 7;
    assert_eq!(trivially(&f)(), 7);
    let offset = 3;
    let add = move |v: i32| v + offset;
    assert_eq!(trivially(&add)(4), 7);
    assert_eq!(trivially(&Point { x: 1, y: -1 }), Point { x: 1, y: -1 });
    let bits = trivially(&Bits { word: 0x01020304 });
    assert_eq!(unsafe { bits.bytes }.len(), 4);
    assert_eq!(unsafe { bits.word }, 0x01020304);
}

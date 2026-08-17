//@ compile-flags: -Zfmt-debug=shallow
// `-Zfmt-debug=shallow` cuts a derived `Debug` down to the type or variant
// name, so the fields are never formatted -- and never even bound.
#![feature(fmt_debug)]
#![allow(dead_code)]

#[derive(Debug)]
struct Named {
    field: u32,
    bomb: Bomb,
}

#[derive(Debug)]
struct Positional(u32, Bomb);

#[derive(Debug)]
enum Choice {
    Unit,
    Tuple(Bomb),
    Struct { bomb: Bomb },
}

struct Bomb;

impl std::fmt::Debug for Bomb {
    fn fmt(&self, _: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        panic!("a shallow Debug must not reach a field")
    }
}

fn main() {
    assert!(cfg!(fmt_debug = "shallow"));
    assert_eq!(format!("{:?}", Named { field: 1, bomb: Bomb }), "Named");
    assert_eq!(format!("{:#?}", Positional(1, Bomb)), "Positional");
    assert_eq!(format!("{:?}", Choice::Unit), "Unit");
    assert_eq!(format!("{:?}", Choice::Tuple(Bomb)), "Tuple");
    assert_eq!(format!("{:#?}", Choice::Struct { bomb: Bomb }), "Struct");

    // A primitive still prints its value.
    assert_eq!(format!("{:?} {:?} {:?}", 1234, true, 3.0), "1234 true 3.0");
}

// A tuple variant or tuple struct used only as a function value still needs the
// type it constructs to be emitted: nothing else here names `B` or `S`.
#![allow(dead_code)]

enum B {
    B1(String),
    B2(String),
}

struct S(u8);

fn main() {
    let flag = 1;
    let _ctor = match flag {
        1 => B::B1,
        _ => B::B2,
    };
    assert_eq!(core::mem::size_of_val(&S), 0);
}

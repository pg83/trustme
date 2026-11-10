#![feature(decl_macro)]
//@ compile-fail: Could not find loop label

macro leave() {
    break 'outer 41u16;
}

fn main() {
    let _value = 'outer: loop {
        leave!();
    };
}

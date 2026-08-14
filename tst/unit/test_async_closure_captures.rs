#![allow(dead_code)]

struct S {
    field: i32,
}

fn make(s: &S, value: &i32) {
    let _ = async || {
        let _ = s.field;
        let _ = value;
    };
}

fn main() {}

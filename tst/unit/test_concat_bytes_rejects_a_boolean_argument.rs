//@ compile-fail: arguments must be
#![feature(concat_bytes)]

fn main() {
    let _ = concat_bytes!(false);
}

//@ compile-fail: array elements must be
#![feature(concat_bytes)]

fn main() {
    let _ = concat_bytes!([true]);
}

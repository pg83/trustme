//@ compile-fail: repeat count must be
#![feature(concat_bytes)]

fn main() {
    let _ = concat_bytes!([b'x'; true]);
}

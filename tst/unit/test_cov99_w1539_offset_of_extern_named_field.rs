//@ compile-fail: no named field `missing`
#![feature(extern_types)]

extern "C" {
    type Foreign;
}

const OFFSET: usize = core::mem::offset_of!(Foreign, missing);

fn main() {
    let _ = OFFSET;
}

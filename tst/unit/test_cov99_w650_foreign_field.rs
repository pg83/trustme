//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Couldn't find the field cov99_w650_missing in
#![feature(extern_types)]

extern "C" {
    type Foreign;
}

pub fn inspect(value: &Foreign) {
    let _ = value.cov99_w650_missing;
}

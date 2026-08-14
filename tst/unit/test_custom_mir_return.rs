#![allow(internal_features)]
#![feature(core_intrinsics, custom_mir)]

use core::intrinsics::mir::*;

#[custom_mir(dialect = "built")]
fn custom_return() {
    mir!({
        Return()
    })
}

fn main() {}

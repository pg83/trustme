// A node a desugaring built has no result type until type checking gives it
// one, and an array size is walked by the passes that run before then.
//@ crate-type: lib
#![allow(while_true)]

pub fn loops() {
    let _ = [(); { while true { break } 0 }];
    let _ = [(); { if true {} 0 }];
    let _ = [(); { if true {} else {} 0 }];
    let _ = [(); { let mut i = 0; while i < 3 { i += 1 } i as usize }];
}

pub const N: usize = { while true { break } 4 };

pub fn sized() -> [u8; N] {
    [0; N]
}

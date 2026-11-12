//@ compile-fail: error:
#![feature(unboxed_closures)]

fn invoke<F: FnOnce<u16>>(callable: F) {
    let _ = callable(239u16);
}

fn main() {}

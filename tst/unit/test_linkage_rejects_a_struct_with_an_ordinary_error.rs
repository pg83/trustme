//@ compile-fail: error:0: linkage may only be applied to functions and statics
#![feature(linkage)]

#[linkage = "weak"]
struct Slot(u16);

fn main() {
    let _ = Slot(811);
}

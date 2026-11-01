//@ compile-fail: Failed to infer const argument
#![feature(generic_arg_infer)]

fn main() {
    let _ = core::mem::size_of::<[u16; _]>();
}

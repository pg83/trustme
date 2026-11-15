//@ compile-only
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(never_type)]

trait Mark {
    fn pointer() -> *const Self {
        loop {}
    }
}
impl Mark for ! {}

pub fn route(pointer: *const !) {
    let mut inferred = <_ as Mark>::pointer();
    inferred = pointer;
    let _ = inferred;
}

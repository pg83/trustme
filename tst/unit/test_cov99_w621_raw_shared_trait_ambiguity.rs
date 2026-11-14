//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ compile-fail: multiple applicable items in scope

trait First {
    fn raw_shared_pick(self);
}

trait Second {
    fn raw_shared_pick(self);
}

impl First for *const u8 {
    fn raw_shared_pick(self) {}
}

impl Second for *const u8 {
    fn raw_shared_pick(self) {}
}

pub fn call(pointer: *mut u8) {
    pointer.raw_shared_pick();
}

//@ compile-fail: Unable to find an implementation of Fn*
//@ compile-flags: --emit=metadata

trait Two: FnOnce(u8) -> u8 + FnOnce(u16) -> u16 {}

fn call(value: &dyn Two) {
    let _ = value(Default::default());
}

fn main() {}

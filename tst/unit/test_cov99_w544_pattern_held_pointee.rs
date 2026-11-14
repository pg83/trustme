//@ compile-only
//@ compile-flags: --emit=metadata

fn make<T>() -> T {
    loop {}
}

fn main() {
    let (mut slot,) = make();
    let _: *const _ = slot;
    slot = &37u8;
}

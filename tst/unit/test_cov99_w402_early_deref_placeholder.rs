//@ compile-fail: the placeholder `_` is not allowed in item signatures

struct Pending {
    value: _,
    tail: u8,
}

const BORROW: &Pending = unsafe { &*(0_usize as *const Pending) };

fn main() {}

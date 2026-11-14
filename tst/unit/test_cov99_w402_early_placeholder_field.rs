//@ compile-fail: the placeholder `_` is not allowed in item signatures

struct Pending {
    value: _,
}

const BORROW: &Pending = &Pending { value: 1_u8 };

fn main() {}

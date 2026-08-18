// Asking `variant_count` for something that is not an enum is a lint, not an
// error: a struct or a union counts as one variant, anything else as none.
#![feature(variant_count)]
#![allow(enum_intrinsics_non_enums)]

use std::mem::variant_count;

enum Three {
    A,
    B,
    C(usize),
}

struct Pair {
    _a: u32,
    _b: *const u8,
}

const COUNT_ENUM: usize = variant_count::<Three>();
const COUNT_STRUCT: usize = variant_count::<Pair>();
const COUNT_BOOL: usize = variant_count::<bool>();
const COUNT_PTR: usize = variant_count::<*const u8>();

fn main() {
    assert_eq!(COUNT_ENUM, 3);
    assert_eq!(COUNT_STRUCT, 1);
    assert_eq!(COUNT_BOOL, 0);
    assert_eq!(COUNT_PTR, 0);
    assert_eq!(variant_count::<Option<u8>>(), 2);
}

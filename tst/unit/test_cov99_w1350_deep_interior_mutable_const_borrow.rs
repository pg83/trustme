//@ compile-fail: A constant may not refer to interior mutable data
//@ compile-flags: --emit=metadata -Cpanic=abort

use std::cell::UnsafeCell;

const BORROW: &UnsafeCell<u8> = &{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{UnsafeCell::new(7)}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}};

fn main() {}

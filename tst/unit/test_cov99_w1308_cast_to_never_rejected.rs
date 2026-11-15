//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Invalid cast

#![feature(never_type)]

fn main() {
    let _ = 1u8 as !;
}

//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Type mismatch

fn main() {
    let _value = if true { 1u8 } else { false };
}

//@ compile-only
//@ compile-fail: `label` is not allowed in `global_asm!`
//@ compile-flags: --emit=metadata -Cpanic=abort

core::arch::global_asm!("", label {});

fn main() {}

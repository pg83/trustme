//@ edition: 2021
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Attr line : BuiltinProcMacro

#[line]
fn main() {}

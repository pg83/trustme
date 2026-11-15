//@ compile-fail: cycle detected when evaluating constant
//@ compile-flags: --emit=metadata -Cpanic=abort

const FIRST: u32 = SECOND;
const SECOND: u32 = FIRST;

fn main() {}

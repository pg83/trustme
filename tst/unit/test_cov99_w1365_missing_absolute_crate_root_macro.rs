//@ edition: 2021
//@ compile-fail: Unknown macro
//@ compile-flags: --emit=metadata -Cpanic=abort

use ::cov99_w1365_absent as missing_root;

missing_root::probe!();

fn main() {}

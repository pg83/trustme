//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

#![feature(staged_api)]
#![stable(feature = "cov99_w518", since = "1.0.0")]

#[unstable(feature = "cov99_w518_unstable", issue = "1")]
pub fn unstable_function() {}

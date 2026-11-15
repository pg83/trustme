//@ compile-flags: --test --emit=metadata -Cpanic=abort
//@ compile-fail: Unexpected token

#[test]
#[should_panic = 17]
fn integer_panic_message() {}

//@ edition: 2021
//@ crate-type: lib
//@ compile-fail: `$t:ty` may be followed by `TOK_PLUS`, which is not allowed for `ty` fragments

macro_rules! rejected_type_follower {
    ($t:ty +) => {};
}

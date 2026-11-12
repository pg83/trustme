//@ compile-fail: Unexpected token
#![feature(closure_lifetime_binder)]

fn main() {
    let _ = for<'a> move 1187u16;
}

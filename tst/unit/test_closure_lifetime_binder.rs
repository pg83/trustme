//@ edition: 2021
// `for<'a> |x: &'a u8| ...` binds lifetimes for the closure itself. The parser
// read the `for` as a loop and rejected the `<`, and the binder's lifetimes
// have to be in scope for the closure's signature and body.
#![feature(closure_lifetime_binder)]

fn main() {
    // A binder whose lifetime is used by a nested closure's argument.
    for<'a> || -> () {
        for<'c> |_: &'a ()| -> () {};
    };

    // Bound lifetime in the signature, and a call through it.
    let f = for<'a> |x: &'a u32| -> &'a u32 { x };
    let v = 5u32;
    assert_eq!(*f(&v), 5);

    // The same for an async closure: the annotation there describes what the
    // future resolves to.
    let _ = for<'a> async || -> () {};
}

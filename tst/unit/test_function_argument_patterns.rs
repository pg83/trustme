//@ compile-fail: patterns aren't allowed in functions without bodies
// A parameter list without a body names its parameters, it does not match
// them: `fn f(&x: &i32);` has nothing to destructure into.
//
// Same shape as the Rust Reference example items/traits.md:282. The sibling
// rule — a refutable pattern such as `fn f(123: i32) {}` — is
// items/traits.md:297.
trait T {
    fn f(&x: &i32);
}

fn main() {}

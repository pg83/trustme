//@ compile-fail: Trait for &mut i32
// `foo(t)` with `t: &mut i32` and `fn foo<X: Trait>(t: X)`: upstream
// `check_argument_types` coerces the argument into the parameter type `X`, and
// `coerce` unifies a known source with a destination that is an inference
// variable, so `X = &mut i32` and `&mut i32: Trait` is then required - the impl
// for `&i32` does not make the argument reborrow to satisfy the bound.
//
// Same shape as the Rustonomicon coercions example (src/coercions.md:14).
trait Trait {}

fn foo<X: Trait>(t: X) {
    let _ = t;
}

impl<'a> Trait for &'a i32 {}

fn main() {
    let t: &mut i32 = &mut 0;
    foo(t);
}

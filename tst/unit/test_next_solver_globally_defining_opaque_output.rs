//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// A defining-scope opaque in an associated-type requirement is an OUTPUT of
// alias-relate: the trait query `foo: Fn() -> u8` is a valid defining use of
// the RPIT and must not reject the builtin fn-item candidate.  Mirrors
// impl-trait/defined-by-trait-resolution.rs.

fn returns_u8(_: impl Fn() -> u8) {}

pub fn foo() -> impl Sized {
    returns_u8(foo);
    0u8
}

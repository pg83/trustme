//@ run-pass
// `x.into()` with `T::Type: Into<T::Type1> + Into<T::Type2>`: the expected
// type `T::Type1` picks the route.  Upstream relates the other route's return
// type `T::Type2` to `T::Type1` by unification (`coerce` ends in `unify` for
// two rigid aliases), and aliases of different items never unify, so that
// route is out.  Deferring the comparison as "not yet normalized" left both
// routes open and the call unresolved (associated-type-bounds
// order-dependent-bounds-issue-54121).
trait Trait {
    type Type: Into<Self::Type1> + Into<Self::Type2> + Copy;
    type Type1;
    type Type2;
}

fn foo<T: Trait>(x: T::Type) -> (T::Type1, T::Type2) {
    let a: T::Type1 = x.into();
    let b: T::Type2 = x.into();
    (a, b)
}

#[derive(Clone, Copy)]
struct Both(u8);
impl From<Both> for u16 {
    fn from(b: Both) -> u16 { b.0 as u16 }
}
impl From<Both> for u32 {
    fn from(b: Both) -> u32 { b.0 as u32 + 1 }
}
struct Impl;
impl Trait for Impl {
    type Type = Both;
    type Type1 = u16;
    type Type2 = u32;
}

fn main() {
    assert_eq!(foo::<Impl>(Both(7)), (7u16, 8u32));
}

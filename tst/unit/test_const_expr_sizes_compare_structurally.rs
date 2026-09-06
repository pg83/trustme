//@ run-pass
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
// `v.test()` returns `[u8; N + 1]` as the trait wrote it, `[0; N + 1]` is written
// here: two abstract consts of the same shape over the same `N`.  Upstream
// relates unevaluated consts structurally (`ConstKind::Expr`), so the array
// impl `PartialEq<[U; N]> for [T; N]` applies and decides the literal as `u8`.
trait Foo<const N: usize> {
    fn test(&self) -> [u8; N + 1];
}

impl<const N: usize> Foo<N> for () {
    fn test(&self) -> [u8; N + 1] {
        [0; N + 1]
    }
}

fn use_dyn<const N: usize>(v: &dyn Foo<N>)
where
    [u8; N + 1]: Sized,
{
    assert_eq!(v.test(), [0; N + 1]);
}

fn main() {
    use_dyn::<3>(&());
}

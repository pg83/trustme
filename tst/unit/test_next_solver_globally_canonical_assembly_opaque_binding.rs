//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// Candidate assembly runs on the canonicalised goal.  Canonicalisation only
// renames variables, so a rigid projection's Opaque path binding must survive
// it: alias-bound candidates (here, `TryType: Try<Residual = Self>` giving
// the `FromResidual` supertrait) are recognised by that binding.  With the
// binding dropped, the only surviving candidate is the environment's
// `R: FromResidual<R::Residual>` supertrait bound, whose self type R then
// wrongly equates with the projection.  Distilled from libcore's
// `array::try_from_fn`.

pub trait FromResidual<R> {
    fn from_residual(residual: R) -> Self;
}
pub trait Try: FromResidual<Self::Residual> {
    type Output;
    type Residual;
}
pub trait Residual<O> {
    type TryType: Try<Output = O, Residual = Self>;
}

type ChangeOutputType<T, V> = <<T as Try>::Residual as Residual<V>>::TryType;

pub fn f<R>() -> ChangeOutputType<R, [R::Output; 3]>
where
    R: Try,
    R::Residual: Residual<[R::Output; 3]>,
{
    FromResidual::from_residual(loop {})
}

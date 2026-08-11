//@ check-pass
//@ compile-flags: -Znext-solver

trait Try {
    type Output;
    type Residual;
}

trait Residual<O> {
    type TryType: Try<Output = O, Residual = Self>;
}

type ChangeOutputType<T, V> = <<T as Try>::Residual as Residual<V>>::TryType;

fn inner<T, R>(_: impl Iterator<Item = R>) -> ChangeOutputType<R, T>
where
    R: Try<Output = T>,
    R::Residual: Residual<T>,
{
    loop {}
}

fn pass<R>(f: impl FnOnce() -> R) -> R {
    f()
}

fn outer<T, R>(
    iter: impl Iterator<Item = T>,
    f: impl FnMut(T) -> R,
) -> ChangeOutputType<R, R::Output>
where
    R: Try,
    R::Residual: Residual<R::Output>,
{
    pass(|| inner(iter.map(f)))
}

fn main() {}

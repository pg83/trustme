//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ compile-fail: ConstProject

pub trait ConstProject<const N: usize> {
    type Out;
}

enum Choice<A, B> {
    Left(A),
    Right(B),
}

pub fn mismatch<T, U, V>(value: <T as ConstProject<1>>::Out)
where
    T: ConstProject<1>,
    U: ConstProject<2>,
    V: ConstProject<2>,
{
    let choice: Choice<<T as ConstProject<1>>::Out, <_ as ConstProject<2>>::Out> =
        Choice::Left(value);
    match choice {
        Choice::Left(x) | Choice::Right(x) => {
            let _ = x;
        }
    }
}

//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ compile-fail: ConstProject

pub trait ConstProject<const N: usize> {
    type Out;
}

pub fn mismatch<T, U, V>()
where
    T: ConstProject<1>,
    U: ConstProject<2>,
    V: ConstProject<2>,
{
    let pair: (<T as ConstProject<1>>::Out, <_ as ConstProject<2>>::Out);
    match pair {
        (x, _) | (_, x) => {
            let _ = x;
        }
    }
}

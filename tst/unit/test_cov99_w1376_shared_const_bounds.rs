//@ edition: 2021
//@ compile-only
//@ compile-flags: --emit=metadata

trait Width<X, const N: usize> {}

fn require<T, X, const N: usize>(_: &T, _: X)
where
    T: Width<X, N>,
{}

fn common<T>(value: &T)
where
    T: Width<u8, 7> + Width<i32, 7>,
{
    require(value, 0);
}

fn main() {}

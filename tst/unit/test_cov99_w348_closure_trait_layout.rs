#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Layout<const N: usize> {
    fn layout(&self) -> usize {
        N
    }
}

impl<T> Layout<{ std::mem::size_of::<T>() }> for T
where
    [(); std::mem::size_of::<T>()]:,
{
}

fn main() {
    let payload = 17_u64;
    let closure = move || payload;
    assert_eq!(closure.layout(), std::mem::size_of_val(&closure));
}

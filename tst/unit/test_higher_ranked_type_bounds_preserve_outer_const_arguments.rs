#![feature(non_lifetime_binders)]

trait Sum<T, const N: usize> {
    fn sum(&self) -> usize;
}

struct Offset<const M: usize>;

impl<T, const N: usize, const M: usize> Sum<T, N> for Offset<M> {
    fn sum(&self) -> usize {
        M + N
    }
}

fn sum_for_all_types<F, const N: usize>(value: &F) -> usize
where
    F: for<T> Sum<T, N>,
{
    <F as Sum<u8, N>>::sum(value) + <F as Sum<(), N>>::sum(value)
}

fn main() {
    assert_eq!(sum_for_all_types::<_, 11>(&Offset::<7>), 36);
    assert_eq!(sum_for_all_types::<_, 3>(&Offset::<5>), 16);
}

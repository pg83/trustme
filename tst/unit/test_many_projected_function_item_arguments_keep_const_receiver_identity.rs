use std::marker::PhantomData;

trait Source { type Item; }
struct Seed;
impl Source for Seed { type Item = u16; }
struct Gate<T, const N: usize>(PhantomData<T>);

impl<T: Source, const N: usize> Gate<T, N> {
    fn lift<A, B, C, D, E, F, G, H, I, J, K, L, M, O, P, Q>(value: T::Item)
        -> (T::Item, usize) { (value, N) }
}

fn invoke<F: FnOnce(A) -> R, A, R>(function: F, value: A) -> R { function(value) }

fn project<T: Source>(value: T::Item) -> (T::Item, usize) {
    let function = Gate::<T, 7>::lift::<
        T::Item, T::Item, T::Item, T::Item, T::Item, T::Item, T::Item, T::Item,
        T::Item, T::Item, T::Item, T::Item, T::Item, T::Item, T::Item, T::Item,
    >;
    invoke(function, value)
}

fn main() { assert_eq!(project::<Seed>(23), (23u16, 7)); }

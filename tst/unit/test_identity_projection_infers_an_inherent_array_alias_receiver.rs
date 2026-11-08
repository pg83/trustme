#![feature(inherent_associated_types)]

struct Storage<T>(core::marker::PhantomData<T>);
impl<T: Copy> Storage<T> {
    type Row<const N: usize> = [T; N];
}

trait Identity {
    type Value: ?Sized;
}
impl<T: ?Sized> Identity for T {
    type Value = T;
}

fn main() {
    let values: <<Storage<_>>::Row<3> as Identity>::Value = [3u16, 5, 8];
    assert_eq!(values, [3u16, 5, 8]);
}

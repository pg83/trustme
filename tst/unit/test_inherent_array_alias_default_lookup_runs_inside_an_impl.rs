#![feature(inherent_associated_types)]

struct Storage<T>(core::marker::PhantomData<T>);
impl<T: Copy> Storage<T> {
    type Row<const N: usize> = [T; N];
}

struct Exercise;
impl Exercise {
    fn run() {
        let values: [u16; 3] = <<Storage<_>>::Row<3>>::default();
        assert_eq!(values, [0u16; 3]);
    }
}

fn main() {
    Exercise::run();
}

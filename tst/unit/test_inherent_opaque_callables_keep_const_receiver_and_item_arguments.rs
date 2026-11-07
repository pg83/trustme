use std::marker::PhantomData;

struct Shelf<T, const WIDTH: usize>(PhantomData<T>);

impl<T, const WIDTH: usize> Shelf<T, WIDTH> {
    fn encoded<U>(_: U) -> usize {
        WIDTH + std::mem::size_of::<T>() + std::mem::size_of::<U>()
    }

    fn emitter<U>() -> impl Fn(U) -> usize + Copy {
        Self::encoded::<U>
    }
}

fn choose<T, const WIDTH: usize>() -> impl Fn(u16) -> usize + Copy {
    Shelf::<T, WIDTH>::emitter::<u16>()
}

fn main() {
    let first = choose::<u64, 257>();
    let second = Shelf::<u8, 4093>::emitter::<u32>();
    assert_eq!(first(3), 267);
    assert_eq!(second(5), 4098);
    assert_eq!(std::mem::size_of_val(&first), 0);
}

trait Width: Copy {
    const WIDTH: usize;
}

impl Width for u8 {
    const WIDTH: usize = 32;
}

const fn opaque() -> impl Width {
    0u8
}

const fn width<T: Width>(_: T) -> usize {
    T::WIDTH
}

const fn return_size<T>(_: fn() -> T) -> usize {
    core::mem::size_of::<T>()
}

const WIDTH: usize = width(opaque());
const SIZE: usize = return_size(opaque);

fn main() {
    assert_eq!(WIDTH, 32);
    assert_eq!(SIZE, 1);
}

trait Spec<T> {
    type Output;
    const LENGTH: usize;
}

struct Width<T, const N: usize>([T; N]);

impl<T, const N: usize> Spec<T> for Width<T, N> {
    type Output = [T; N];
    const LENGTH: usize = N;
}

impl<T, const N: usize> Width<T, N> {
    const fn metric<U>() -> usize {
        <Self as Spec<T>>::LENGTH + std::mem::size_of::<T>() + std::mem::size_of::<U>()
    }
}

const fn adjusted<T>(value: usize) -> usize {
    value + std::mem::size_of::<T>()
}

trait Read<T> {
    type Output;
    fn read(&self) -> Self::Output;
}

impl<const N: usize> Read<u8> for Width<u8, N> {
    type Output = usize;
    fn read(&self) -> usize { self.0[0] as usize + N }
}

const METRIC: usize = adjusted::<u32>(Width::<u8, 3>::metric::<u16>());
const SIZE: usize = std::mem::size_of::<<Width<u16, 3> as Spec<u16>>::Output>();
static VALUE: Width<u8, 3> = Width([11, 13, 17]);
const READER: &dyn Read<u8, Output = usize> = &VALUE;

fn main() {
    assert_eq!(METRIC, 10);
    assert_eq!(SIZE, 6);
    assert_eq!(READER.read(), 14);
    let other: &dyn Read<u8, Output = usize> = &Width([19, 23, 29, 31, 37]);
    assert_eq!(other.read(), 24);
}

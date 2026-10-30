trait Width<const N: usize, T> {
    fn array(value: T) -> [T; N];
}

struct Builder;

impl<T: Copy, const N: usize> Width<N, T> for Builder {
    fn array(value: T) -> [T; N] {
        [value; N]
    }
}

struct Slots<const N: usize, T> {
    values: [T; N],
}

fn main() {
    let first: [u8; 3] = <Builder as Width<_, u8>>::array(7);
    let second: Slots<_, u16> = Slots { values: [11; 5] };
    assert_eq!(first, [7; 3]);
    assert_eq!(second.values, [11; 5]);
}

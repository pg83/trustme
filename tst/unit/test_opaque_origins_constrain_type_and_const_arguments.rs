fn sequence<T: Copy>(value: T) -> impl Iterator<Item = T> + Clone {
    std::iter::once(value)
}

struct Maker<T>(T);
impl<T: Copy> Maker<T> {
    fn sequence<const N: usize>(self) -> impl Iterator<Item = T> + Clone {
        [self.0; N].into_iter()
    }
}

fn bytes<const N: usize>() -> impl AsRef<[u8]> + Copy {
    [N as u8; N]
}

fn join<T: Iterator<Item = u8>>(first: T, second: T) -> Vec<u8> {
    first.chain(second).collect()
}

fn compare<T: AsRef<[u8]>>(first: T, second: T) {
    assert_eq!(first.as_ref(), &[3, 3, 3]);
    assert_eq!(second.as_ref(), first.as_ref());
}

fn main() {
    assert_eq!(join(sequence(7u8), sequence(9)), vec![7, 9]);
    assert_eq!(join(sequence(11), sequence(13u8)), vec![11, 13]);
    assert_eq!(join(Maker(17u8).sequence::<2>(), Maker(19).sequence::<2>()), vec![17, 17, 19, 19]);
    compare(bytes::<3>(), bytes());
}

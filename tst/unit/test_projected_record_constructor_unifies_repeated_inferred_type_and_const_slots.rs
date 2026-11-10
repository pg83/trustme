#![feature(generic_arg_infer)]

trait Layout<A, B, const N: usize> {
    type Buffer;
}

struct Factory;
struct Record<P, const N: usize> {
    pair: P,
    bytes: [u8; N],
}

impl<A, B, const N: usize> Layout<A, B, N> for Factory {
    type Buffer = Record<(A, B), N>;
}

impl<T, const N: usize> Record<(T, T), N> {
    fn from_parts(first: T, second: T, bytes: [u8; N]) -> Self {
        Record { pair: (first, second), bytes }
    }
}

fn main() {
    let numbers = <Factory as Layout<_, _, _>>::Buffer::from_parts(17u16, 23u16, [3, 5, 7, 11]);
    assert_eq!(numbers.pair, (17, 23));
    assert_eq!(numbers.bytes, [3, 5, 7, 11]);
    let flags = <Factory as Layout<_, _, _>>::Buffer::from_parts(true, false, []);
    assert_eq!(flags.pair, (true, false));
    assert_eq!(flags.bytes.len(), 0);
}

//@ compile-variant: -O
#![feature(more_qualified_paths)]

struct Record<T> { value: T }
struct Source<const N: usize>;
trait Project { type Output; }
impl<const N: usize> Project for Source<N> { type Output = Record<[u16; N]>; }
const fn length() -> usize { 3 }

fn read<const N: usize>(record: <Source<N> as Project>::Output) -> [u16; N] {
    let <Source<N> as Project>::Output { value } = record;
    value
}

fn main() {
    let <Source<{ length() }> as Project>::Output { value } = Record { value: [947u16, 953, 967] };
    assert_eq!(value, [947, 953, 967]);
    assert_eq!(read::<2>(Record { value: [971, 977] }), [971, 977]);
}

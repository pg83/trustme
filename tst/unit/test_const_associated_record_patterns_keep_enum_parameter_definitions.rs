//@ compile-variant: -O
#![feature(more_qualified_paths)]

struct Record<T> { value: T }
enum Source<const N: usize> { Values([u16; N]) }
trait Project { type Output; }
impl<const N: usize> Project for Source<N> { type Output = Record<[u16; N]>; }

fn main() {
    let <Source<{ 1 + 2 }> as Project>::Output { value } = Record { value: [983u16, 991, 997] };
    let Source::Values(values) = Source::Values(value);
    assert_eq!(values, [983, 991, 997]);
}

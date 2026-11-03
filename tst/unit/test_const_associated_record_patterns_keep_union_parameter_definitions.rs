//@ compile-variant: -O
#![feature(more_qualified_paths)]

struct Record<T> { value: T }
union Source<const N: usize> { values: [u16; N] }
trait Project { type Output; }
impl<const N: usize> Project for Source<N> { type Output = Record<[u16; N]>; }

fn main() {
    let <Source<{ 2 + 1 }> as Project>::Output { value } = Record { value: [1009u16, 1013, 1019] };
    let source = Source { values: value };
    assert_eq!(unsafe { source.values }, [1009, 1013, 1019]);
}

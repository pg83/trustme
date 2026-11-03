//@ compile-fail: Type mismatch
#![feature(more_qualified_paths)]

struct Record<T> { value: T }
enum Source<const N: usize> { Values([u16; N]) }
trait Project { type Output; }
impl<const N: usize> Project for Source<N> { type Output = Record<[u16; N]>; }

fn main() {
    let <Source<3> as Project>::Output { value } = Record { value: [1021u16, 1031] };
    let _ = value;
}

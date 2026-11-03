//@ compile-variant: -O
#![feature(more_qualified_paths)]
struct Record<T> { value: T }
trait Project { type Output; }
struct Identity<T, const N: usize>([T; N]);
impl<T, const N: usize> Project for Identity<T, N> {
    type Output = Record<[T; N]>;
}
fn take<T, const N: usize>(record: <Identity<T, N> as Project>::Output) -> [T; N] {
    let <Identity<T, N> as Project>::Output { value } = record;
    value
}
fn main() {
    let <Identity<u16, 3> as Project>::Output { value } = Record { value: [811, 821, 823] };
    assert_eq!(value, [811, 821, 823]);
    let answer = take::<u16, 2>(Record { value: [827, 829] });
    assert_eq!(answer, [827, 829]);
}

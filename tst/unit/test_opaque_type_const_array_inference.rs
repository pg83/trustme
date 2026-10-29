#![feature(generic_const_exprs, type_alias_impl_trait)]
fn hidden<const N: usize>() -> impl AsRef<[u8]> + Copy { [3u8; N] }
type Buffer<const N: usize> = impl Copy;
#[define_opaque(Buffer)]
fn buffer<const N: usize>() -> Buffer<N> { [11u16; N] }
fn bytes<T>(_: T) -> [u8; std::mem::size_of::<T>()]
where [(); std::mem::size_of::<T>()]: {
    [7; std::mem::size_of::<T>()]
}
fn length<const N: usize>(array: [u8; N]) -> usize {
    assert!(array.iter().all(|value| *value == 7));
    N
}
fn main() {
    assert_eq!(length(bytes(hidden::<5>())), 5);
    assert_eq!(length(bytes(hidden::<11>())), 11);
    assert_eq!(length(bytes(buffer::<3>())), 6);
    assert_eq!(length(bytes(buffer::<7>())), 14);
}

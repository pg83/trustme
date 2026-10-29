#![feature(generic_const_exprs, type_alias_impl_trait)]
trait Width<const N: usize> {}
impl<T> Width<{ std::mem::size_of::<T>() }> for T
where [(); std::mem::size_of::<T>()]: {}
fn zeros<T: Width<N>, const N: usize>(_: T) -> [u8; N] { [0; N] }
fn hidden<const N: usize>() -> impl Copy { [1u8; N] }
type Buffer<const N: usize> = impl Copy;
#[define_opaque(Buffer)]
fn buffer<const N: usize>() -> Buffer<N> { [3u16; N] }
fn main() {
    assert_eq!(zeros(hidden::<3>()), [0; 3]);
    assert_eq!(zeros(hidden::<7>()), [0; 7]);
    assert_eq!(zeros(buffer::<2>()), [0; 4]);
    assert_eq!(zeros(buffer::<5>()), [0; 10]);
}

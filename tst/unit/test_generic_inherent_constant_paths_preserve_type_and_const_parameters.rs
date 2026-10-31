struct Packet<T, const N: usize>(std::marker::PhantomData<T>);
impl<T, const N: usize> Packet<T, N> {
    const BYTES: [u8; N] = [7; N];
    const WIDTH: usize = std::mem::size_of::<T>();
}
fn inspect<T, const N: usize>() -> (usize, usize) { (Packet::<T, N>::BYTES.len(), Packet::<T, N>::WIDTH) }
fn main() { assert_eq!(inspect::<u16, 3>(), (3, 2)); assert_eq!(inspect::<u64, 5>(), (5, 8)); }

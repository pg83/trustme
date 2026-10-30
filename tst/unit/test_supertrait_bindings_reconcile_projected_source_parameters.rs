trait Family { type Arg; }
impl Family for () { type Arg = u8; }
trait Base<T, U, const N: usize> {
    type Output;
    type Echo;
    fn get(&self, value: T) -> Self::Output;
    fn echo(&self) -> Self::Echo;
}
trait Child<T, U, const N: usize>: Base<T, U, N> {}
impl Base<u8, bool, 3> for () {
    type Output = [u8; 3];
    type Echo = u16;
    fn get(&self, value: u8) -> Self::Output { [value; 3] }
    fn echo(&self) -> Self::Echo { 41 }
}
impl Child<u8, bool, 3> for () {}
fn opaque<T: Family<Arg = u8>>() -> impl Child<T::Arg, bool, 3, Output = [u8; 3], Echo = u16> { () }
fn read<T: Family<Arg = u8>>(value: &dyn Child<T::Arg, bool, 3, Output = [u8; 3], Echo = u16>) -> ([u8; 3], [u8; 3], u16) {
    (value.get(17), opaque::<T>().get(23), value.echo())
}
fn main() { assert_eq!(read::<()>(&()), ([17; 3], [23; 3], 41)); }

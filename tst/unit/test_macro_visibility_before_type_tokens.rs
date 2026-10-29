macro_rules! pointer { ($v:vis *const $ty:ty) => { $v type Pointer = *const $ty; }; }
macro_rules! reference { ($v:vis &'static $ty:ty) => { $v type Reference = &'static $ty; }; }
macro_rules! tuple { ($v:vis ($left:ty, $right:ty)) => { $v type Pair = ($left, $right); }; }
macro_rules! array { ($v:vis [$ty:ty; $n:expr]) => { $v type Array = [$ty; $n]; }; }
pointer!(pub *const u16);
reference!(&'static u16);
tuple!(pub(crate) (u8, u16));
array!([u8; 3]);
static VALUE: u16 = 11;
fn main() {
    let pointer: Pointer = &VALUE;
    let reference: Reference = &VALUE;
    let pair: Pair = (3, 7);
    let array: Array = [2, 3, 5];
    assert_eq!(unsafe { *pointer }, *reference);
    assert_eq!(pair.0 as u16 + pair.1 + array[2] as u16, 15);
}

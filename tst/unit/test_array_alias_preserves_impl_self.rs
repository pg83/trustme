#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
type Buffer<T> = [u8; std::mem::size_of::<T>() + 1];
trait Storage { type Bytes; }
struct Small(u16);
struct Large([u8; 9]);
impl Storage for Small { type Bytes = Buffer<Self>; }
impl Storage for Large { type Bytes = Buffer<Self>; }
fn main() {
    let small: <Small as Storage>::Bytes = [3; 3];
    let large: <Large as Storage>::Bytes = [7; 10];
    assert_eq!(small.len(), 3);
    assert_eq!(large.len(), 10);
    assert_eq!(small[2] + large[9], 10);
    assert_eq!(Small(11).0, 11);
    assert_eq!(Large([4; 9]).0[8], 4);
}

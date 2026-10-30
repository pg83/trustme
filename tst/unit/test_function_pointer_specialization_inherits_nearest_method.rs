#![feature(specialization)]
#![allow(incomplete_features)]
trait Rank { fn rank()->u8; fn inherited()->u8; }
impl Rank for fn([u8;3])->[u8;3] { fn rank()->u8 {3} }
impl<T> Rank for fn(T)->T {
 default fn rank()->u8 {1}
 default fn inherited()->u8 {9}
}
impl<const N:usize> Rank for fn([u8;N])->[u8;N] {
 default fn rank()->u8 {2}
 default fn inherited()->u8 {19}
}
fn main() {
 assert_eq!(<fn(u8)->u8 as Rank>::rank(),1);
 assert_eq!(<fn([u8;2])->[u8;2] as Rank>::rank(),2);
 assert_eq!(<fn([u8;3])->[u8;3] as Rank>::rank(),3);
 assert_eq!(<fn([u8;3])->[u8;3] as Rank>::inherited(),19);
}

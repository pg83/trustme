#![feature(arbitrary_self_types)]
use std::marker::PhantomData;
use std::ops::Deref;
trait Family {
    type Item;
    fn bytes(self:Carrier<Self,Self::Item>)->usize { std::mem::size_of_val(&self.item) }
}
struct Carrier<T:Family+?Sized,U:?Sized> { marker:PhantomData<T>, item:U }
impl<T:Family+?Sized> Deref for Carrier<T,T::Item> {
    type Target=T;
    fn deref(&self)->&T { panic!("receiver lookup must not evaluate deref") }
}
impl Family for u8 { type Item=u16; }
impl Family for u16 { type Item=[u8;7]; }
fn main() {
    let small=Carrier::<u8,u16>{marker:PhantomData,item:19};
    let large=Carrier::<u16,[u8;7]>{marker:PhantomData,item:[17;7]};
    assert_eq!(small.bytes(),2);
    assert_eq!(large.bytes(),7);
}

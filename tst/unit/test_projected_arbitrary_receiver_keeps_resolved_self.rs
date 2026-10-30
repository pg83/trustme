#![feature(arbitrary_self_types)]
use std::marker::PhantomData;
use std::ops::Deref;
trait Family {
    type Item;
    fn value(self:Carrier<Self,Self::Item>)->u16;
}
struct Carrier<T:Family+?Sized,U:?Sized> { marker:PhantomData<T>, item:U }
impl<T:Family+?Sized> Deref for Carrier<T,T::Item> {
    type Target=T;
    fn deref(&self)->&T { panic!("receiver lookup must not evaluate deref") }
}
impl Family for u8 {
    type Item=u16;
    fn value(self:Carrier<Self,Self::Item>)->u16 { self.item }
}
fn main() {
    let carrier=Carrier::<u8,u16>{marker:PhantomData,item:19};
    assert_eq!(carrier.value(),19);
}

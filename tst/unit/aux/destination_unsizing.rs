#![feature(coerce_unsized)]
use std::ops::CoerceUnsized;
use std::marker::PhantomData;
pub trait Value { fn value(&self) -> u32; }
impl Value for u32 { fn value(&self) -> u32 { *self } }
pub struct Tuple<A: ?Sized, B: ?Sized>(pub *const A, pub *const B);
impl CoerceUnsized<Tuple<[u16], u32>> for Tuple<[u16; 3], u32> {}
impl CoerceUnsized<Tuple<[u16; 3], dyn Value>> for Tuple<[u16; 3], u32> {}
pub struct Named<A: ?Sized, B: ?Sized> {
    pub tag: u16, pub first: *const A, pub marker: PhantomData<(*const A, *const B)>, pub second: *const B,
}
impl CoerceUnsized<Named<[u16], u32>> for Named<[u16; 3], u32> {}
impl CoerceUnsized<Named<[u16; 3], dyn Value>> for Named<[u16; 3], u32> {}
pub static DATA: [u16; 3] = [211, 223, 227];
pub static NUMBER: u32 = 229;
pub const SOURCE: Tuple<[u16; 3], u32> = Tuple(&DATA as *const [u16; 3], &NUMBER as *const u32);
pub const SLICE: Tuple<[u16], u32> = SOURCE;
pub const DYNAMIC: Tuple<[u16; 3], dyn Value> = SOURCE;

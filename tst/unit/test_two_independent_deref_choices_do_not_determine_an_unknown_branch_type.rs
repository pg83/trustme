//@ compile-fail: Type mismatch
use std::marker::PhantomData;
use std::ops::Deref;
struct Left<T>(PhantomData<T>);
struct Right<T>(PhantomData<T>);
static RIGHT32: Right<u32> = Right(PhantomData);
static RIGHT64: Right<u64> = Right(PhantomData);
static LEFT32: Left<u32> = Left(PhantomData);
static LEFT64: Left<u64> = Left(PhantomData);
impl Deref for Left<u8> {
    type Target = Right<u32>;
    fn deref(&self) -> &Self::Target { &RIGHT32 }
}
impl Deref for Left<u16> {
    type Target = Right<u64>;
    fn deref(&self) -> &Self::Target { &RIGHT64 }
}
impl Deref for Right<u8> {
    type Target = Left<u32>;
    fn deref(&self) -> &Self::Target { &LEFT32 }
}
impl Deref for Right<u16> {
    type Target = Left<u64>;
    fn deref(&self) -> &Self::Target { &LEFT64 }
}
fn main() {
    let left = Left(PhantomData);
    let right = Right(PhantomData);
    let _joined = if std::hint::black_box(true) { &left } else { &right };
}

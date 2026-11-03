#![feature(coerce_unsized, unsize)]
use std::marker::{PhantomData, Unsize};
use std::ops::CoerceUnsized;

struct View<T: ?Sized, M, const N: usize> { pointer: *const T, marker: PhantomData<M>, bytes: [u8; N] }
impl<T: ?Sized + Unsize<U>, U: ?Sized, M, const N: usize> CoerceUnsized<View<U, M, N>> for View<T, M, N> {}
struct Nested<T: ?Sized, M, const N: usize>(View<T, M, N>);
impl<T: ?Sized + Unsize<U>, U: ?Sized, M, const N: usize> CoerceUnsized<Nested<U, M, N>> for Nested<T, M, N> {}
trait Number { fn number(&self) -> u32; }
impl Number for u32 { fn number(&self) -> u32 { *self } }

fn main() {
    let array = [11u8, 17, 23];
    let source: View<[u8; 3], u16, 5> = View { pointer: &array, marker: PhantomData, bytes: [7; 5] };
    let slice: View<[u8], _, _> = source;
    assert_eq!(unsafe { &*slice.pointer }, &[11, 17, 23]);
    assert_eq!(slice.bytes, [7; 5]);
    let value = 79u32;
    let source: Nested<u32, (), 2> = Nested(View { pointer: &value, marker: PhantomData, bytes: [3; 2] });
    let object: Nested<dyn Number, _, _> = source;
    assert_eq!(unsafe { (&*object.0.pointer).number() }, 79);
    assert_eq!(object.0.bytes, [3; 2]);
}

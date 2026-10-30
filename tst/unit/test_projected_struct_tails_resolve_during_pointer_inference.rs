#![feature(coerce_unsized,unsize)]
use std::ops::CoerceUnsized;
use std::marker::Unsize;
struct Pointer<T:?Sized>(*const T);
impl<T:?Sized+Unsize<U>,U:?Sized> CoerceUnsized<Pointer<U>> for Pointer<T> {}
trait Shape { type Tail: ?Sized; }
struct Word;
impl Shape for Word { type Tail=u32; }
struct Named<S: Shape> { head:u8, tail:S::Tail }
struct Tuple<S: Shape>(u8,S::Tail);
trait Read { fn read(&self) -> u32; }
impl Read for Named<Word> { fn read(&self) -> u32 { self.head as u32+self.tail } }
impl Read for Tuple<Word> { fn read(&self) -> u32 { self.0 as u32+self.1 } }
#[inline(never)]
fn named(value:&Named<Word>) -> &dyn Read { value }
#[inline(never)]
fn tuple(value:&Tuple<Word>) -> &dyn Read { value }
fn main() {
 let value=Named::<Word>{head:3,tail:17};
 let pair=Tuple::<Word>(5,19);
 let unresolved=Pointer(0 as *const _);
 let resolved:Pointer<Named<Word>>=unresolved;
 assert!(resolved.0.is_null());
 let unresolved=Pointer(0 as *const _);
 let resolved:Pointer<Tuple<Word>>=unresolved;
 assert!(resolved.0.is_null());
 assert_eq!(named(&value).read(),20);
 assert_eq!(tuple(&pair).read(),24);
 assert_eq!(std::mem::size_of_val(&value),8);
 assert_eq!(std::mem::size_of_val(&pair),8);
}

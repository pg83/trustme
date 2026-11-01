#![feature(specialization)]
#![allow(incomplete_features)]

trait Pick { fn code() -> u8; }
impl<A, B> Pick for (A, B) { default fn code() -> u8 { 1 } }
impl<T> Pick for (T, T) { fn code() -> u8 { 2 } }
trait View { fn value(&self) -> u8; }
struct Number;
impl View for Number { fn value(&self) -> u8 { 7 } }
trait Other {}
impl Other for Number {}

fn main() {
    assert_eq!(<(fn(u16), fn(u16)) as Pick>::code(), 2);
    assert_eq!(<(fn(u16), fn(u8)) as Pick>::code(), 1);
    assert_eq!(<(fn(u16), fn(u16, u8)) as Pick>::code(), 1);
    assert_eq!(<(fn(u16), unsafe fn(u16)) as Pick>::code(), 1);
    assert_eq!(<(fn(u16), extern "C" fn(u16)) as Pick>::code(), 1);
    assert_eq!(<(*const u8, *mut u8) as Pick>::code(), 1);
    assert_eq!(<(&u8, &mut u8) as Pick>::code(), 1);
    assert_eq!(<([u8; 2], [u8; 3]) as Pick>::code(), 1);
    assert_eq!(<((u8, u16), (u8, u32)) as Pick>::code(), 1);
    assert_eq!(<(&dyn View, &(dyn View + Send)) as Pick>::code(), 1);
    assert_eq!(<(&(dyn View + Send), &(dyn View + Sync)) as Pick>::code(), 1);
    assert_eq!(<(&dyn View, &dyn Other) as Pick>::code(), 1);
    assert_eq!(<(&dyn View, &dyn View) as Pick>::code(), 2);
    assert_eq!(Number.value(), 7);
}

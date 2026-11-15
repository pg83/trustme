//@ compile-only
//@ compile-flags: --emit=llvm-ir -Cpanic=abort -Z dump-hir -Z dump-mir
#![feature(adt_const_params, unsized_const_params)]
#![allow(incomplete_features)]

static EMPTY: [u8; 0] = [];
const RELOCATED: &[u8] = &EMPTY;
const ABSOLUTE: &[u8] = unsafe { &*(0x1000usize as *const [u8; 0]) };

trait Base<const BYTES: &'static [u8]> {
    fn value(&self) -> u8;
}

trait Child<const BYTES: &'static [u8]>: Base<BYTES> {}

struct Item(u8);

impl<const BYTES: &'static [u8]> Base<BYTES> for Item {
    fn value(&self) -> u8 {
        self.0
    }
}

impl<const BYTES: &'static [u8]> Child<BYTES> for Item {}

struct Deferred<T>(core::marker::PhantomData<T>);

impl<T> Deferred<T> {
    const VALUE: &'static dyn Child<RELOCATED> =
        &Item(core::mem::size_of::<T>() as u8);
}

#[inline(never)]
fn late<T>() -> &'static dyn Child<RELOCATED> {
    Deferred::<T>::VALUE
}

fn main() {
    let first: &dyn Base<ABSOLUTE> = &Item(0);
    let second = late::<u8>();
    let _ = (
        core::hint::black_box(first).value(),
        core::hint::black_box(second).value(),
    );
}

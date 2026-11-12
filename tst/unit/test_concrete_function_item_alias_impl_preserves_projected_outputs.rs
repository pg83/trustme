//@ compile-flags: -Cpanic=abort

#![no_std]
#![no_main]
#![feature(type_alias_impl_trait)]
#![allow(type_alias_bounds)]

trait Types {
    type Argument;
}

struct Word;
impl Types for Word {
    type Argument = u16;
}

struct Bytes;
impl Types for Bytes {
    type Argument = [u8; 3];
}

type Callable<T: Types> = impl Fn() -> u16;

fn source<U>() -> u16 {
    core::mem::size_of::<U>() as u16 + 631
}

#[define_opaque(Callable)]
fn make<T: Types>() -> Callable<T> {
    source::<T::Argument>
}

trait Label {
    type Output;
    fn value(&self) -> Self::Output;
}

impl Label for Callable<Word> {
    type Output = u16;

    fn value(&self) -> u16 {
        self()
    }
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    loop {}
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    let word: <Callable<Word> as Label>::Output = make::<Word>().value();
    let bytes = make::<Bytes>()();
    if word == 633 && bytes == 634 { 0 } else { 1 }
}

//@ compile-flags: -Clink-arg=-lc
#![feature(auto_traits, lang_items, no_core)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"]
trait PointeeSized {}
#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}
#[lang = "sized"]
trait Sized: MetaSized {}

#[lang = "freeze"]
unsafe auto trait Freeze {}

#[lang = "copy"]
trait Copy {}
impl Copy for u16 {}
impl Copy for i32 {}
impl Copy for *const u16 {}

#[lang = "legacy_receiver"]
trait LegacyReceiver: PointeeSized {}
impl<T: PointeeSized> LegacyReceiver for &T {}
impl<T: PointeeSized> LegacyReceiver for &mut T {}

#[lang = "deref"]
trait Deref {
    #[lang = "deref_target"]
    type Target;
    fn deref(&self) -> &Self::Target;
}

impl Deref for *const u16 {
    type Target = u16;

    fn deref(&self) -> &u16 {
        unsafe { &**self }
    }
}

trait ReadValue {
    fn read_value(&self) -> u16;
}

impl ReadValue for u16 {
    fn read_value(&self) -> u16 {
        *self
    }
}

fn through_bound<T: Deref + Copy>(pointer: T) -> u16
where
    T::Target: ReadValue,
{
    pointer.read_value()
}

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    let value = 163u16;
    let pointer = &value as *const u16;
    let explicit = <*const u16 as Deref>::deref(&pointer).read_value();
    let generic = through_bound(pointer);
    match (explicit, generic) {
        (163, 163) => 0,
        _ => 1,
    }
}

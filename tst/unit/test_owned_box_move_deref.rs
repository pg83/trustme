#![feature(auto_traits, lang_items, no_core)]
#![no_core]
#![no_main]
//@ compile-flags: -Clink-arg=-lc

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "copy"]
pub trait Copy {}

impl Copy for i32 {}

#[lang = "freeze"]
pub unsafe auto trait Freeze {}

#[lang = "legacy_receiver"]
pub trait LegacyReceiver: PointeeSized {}

impl<T: PointeeSized> LegacyReceiver for &T {}
impl<T: PointeeSized> LegacyReceiver for &mut T {}

#[lang = "coerce_unsized"]
pub trait CoerceUnsized<T> {}

#[lang = "drop"]
pub trait Drop {
    fn drop(&mut self);
}

#[lang = "destruct"]
pub trait Destruct {}

#[lang = "drop_in_place"]
pub unsafe fn drop_in_place<T: PointeeSized>(_to_drop: *mut T) {}

#[lang = "deref"]
pub trait Deref {
    type Target;

    fn deref(&self) -> &Self::Target;
}

#[lang = "owned_box"]
struct Owner<T>(Unique<T>);

struct Unique<T>(NonNull<T>);

struct NonNull<T>(*mut T);

impl<T> Deref for Owner<T> {
    type Target = T;

    fn deref(&self) -> &T {
        unsafe { &*self.0.0.0 }
    }
}

impl<T> Drop for Owner<T> {
    fn drop(&mut self) {}
}

struct Payload(i32);

static mut SLOT: Payload = Payload(37);

fn into_inner<T>(owner: Owner<T>) -> T {
    *owner
}

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    let value = unsafe { into_inner(Owner(Unique(NonNull(&mut SLOT as *mut Payload)))) };
    match value.0 {
        37 => 0,
        _ => 1,
    }
}

#![feature(auto_traits, lang_items, no_core)]
#![allow(path_statements)]
#![no_core]
#![no_main]
//@ compile-flags: -Cpanic=abort -Clink-arg=-lc

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "legacy_receiver"]
pub trait LegacyReceiver: PointeeSized {}

impl<T: PointeeSized> LegacyReceiver for &T {}
impl<T: PointeeSized> LegacyReceiver for &mut T {}

#[lang = "copy"]
pub trait Copy {}

impl Copy for bool {}
impl Copy for u32 {}

#[lang = "freeze"]
pub unsafe auto trait Freeze {}

#[lang = "unpin"]
pub auto trait Unpin {}

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

#[lang = "panic_in_cleanup"]
fn panic_in_cleanup() -> ! {
    loop {}
}

#[lang = "deref"]
pub trait Deref {
    type Target;

    fn deref(&self) -> &Self::Target;
}

#[lang = "owned_box"]
struct Owner<T, A>(Unique<T>, A);

struct Unique<T>(NonNull<T>);

struct NonNull<T>(*mut T);

impl<T, A> Deref for Owner<T, A> {
    type Target = T;

    fn deref(&self) -> &T {
        unsafe { &*self.0.0.0 }
    }
}

impl<T, A> Drop for Owner<T, A> {
    fn drop(&mut self) {}
}

static mut PAYLOAD_DROPPED: bool = false;
static mut ALLOCATOR_DROPS: u32 = 0;

struct Payload;

impl Drop for Payload {
    fn drop(&mut self) {
        unsafe {
            PAYLOAD_DROPPED = true;
        }
    }
}

struct Allocator;

impl Drop for Allocator {
    fn drop(&mut self) {
        unsafe {
            ALLOCATOR_DROPS = match ALLOCATOR_DROPS {
                0 => 1,
                _ => 2,
            };
        }
    }
}

static mut SLOT: Payload = Payload;

fn take_payload() -> bool {
    true
}

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    {
        let owner = unsafe {
            Owner(Unique(NonNull(&mut SLOT as *mut Payload)), Allocator)
        };
        if take_payload() {
            *owner;
        } else {
            owner;
        }
    }

    unsafe {
        match (PAYLOAD_DROPPED, ALLOCATOR_DROPS) {
            (true, 1) => 0,
            _ => 1,
        }
    }
}

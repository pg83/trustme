#![feature(lang_items, no_core, start)]
#![allow(path_statements)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

#[lang = "coerce_unsized"]
pub trait CoerceUnsized<T> {}

#[lang = "drop"]
pub trait Drop {
    fn drop(&mut self);
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
            ALLOCATOR_DROPS += 1;
        }
    }
}

static mut SLOT: Payload = Payload;

fn take_payload() -> bool {
    true
}

fn main() -> i32 {
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
        if PAYLOAD_DROPPED && ALLOCATOR_DROPS == 1 { 0 } else { 1 }
    }
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}

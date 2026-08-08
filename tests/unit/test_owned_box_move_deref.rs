#![feature(lang_items, no_core, start)]
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

fn main() -> i32 {
    let value = unsafe { into_inner(Owner(Unique(NonNull(&mut SLOT as *mut Payload)))) };
    match value.0 {
        37 => 0,
        _ => 1,
    }
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}

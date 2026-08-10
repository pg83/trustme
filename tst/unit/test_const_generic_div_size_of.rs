#![feature(const_trait_impl, intrinsics, lang_items, no_core, start)]
#![no_core]

pub mod ops {
    pub struct RangeFull;
}

#[lang = "sized"]
pub trait Sized {}

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "pointee_trait"]
pub trait Pointee: PointeeSized {
    #[lang = "metadata_type"]
    type Metadata;
}

#[lang = "coerce_unsized"]
pub trait CoerceUnsized<T> {}

#[lang = "fn_ptr_trait"]
pub trait FnPtr {}

#[lang = "tuple_trait"]
pub trait Tuple {}

#[lang = "discriminant_kind"]
pub trait DiscriminantKind {
    #[lang = "discriminant_type"]
    type Discriminant;
}

#[const_trait]
#[lang = "div"]
pub trait Div<Rhs = Self> {
    type Output;

    fn div(self, rhs: Rhs) -> Self::Output;
}

extern "rust-intrinsic" {
    fn size_of<T>() -> usize;
}

struct TypeId {
    data: [*const (); 16 / size_of::<*const ()>()],
}

impl TypeId {}

impl const Div for usize {
    type Output = usize;

    fn div(self, _rhs: usize) -> usize {
        2
    }
}

fn main() -> i32 {
    0
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    0
}

#[panic_handler]
fn panic(_payload: usize) -> u32 {
    1
}

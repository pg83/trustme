#![feature(const_trait_impl, intrinsics, lang_items, no_core, rustc_attrs)]
#![no_core]
#![no_main]
//@ compile-flags: -Clink-arg=-lc

pub mod ops {
    pub struct RangeFull;
}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "copy"]
pub trait Copy {}

impl Copy for usize {}
impl<T: PointeeSized> Copy for *const T {}

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

#[rustc_intrinsic]
const fn size_of<T>() -> usize;

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

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    0
}

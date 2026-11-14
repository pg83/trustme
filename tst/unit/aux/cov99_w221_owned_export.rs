#![feature(type_alias_impl_trait)]

pub trait FirstArg {
    type Arg;
}

impl<A> FirstArg for fn(A) {
    type Arg = A;
}

pub type Exported = impl FirstArg;

#[define_opaque(Exported)]
pub fn hidden() -> Exported {
    unsafe { std::mem::zeroed::<cov99_w221_vtable_type::vtable_type!()>() }.take
}

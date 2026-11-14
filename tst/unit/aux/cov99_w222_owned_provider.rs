#![feature(type_alias_impl_trait)]

pub type Hidden = impl Sized;

fn constrain(_: Hidden) {}

#[define_opaque(Hidden)]
pub fn define_hidden() {
    let _ = cov99_w222_owned_tokens::vtable!();
}

pub trait Expose {
    type Receiver;
}

pub struct Export;

impl Expose for Export {
    type Receiver = Hidden;
}

#![feature(type_alias_impl_trait)]

trait Output {
    type Value;
}

impl<R, F: FnMut() -> R> Output for F {
    type Value = R;
}

type Opaque = impl Output<Value = impl Send>;

#[define_opaque(Opaque)]
fn make() -> Opaque {
    || 1i32
}

fn main() {}

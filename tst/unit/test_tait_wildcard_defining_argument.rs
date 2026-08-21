#![feature(type_alias_impl_trait)]

type Hidden = impl Sized;

trait Super<T> {}
trait Sub: Super<Hidden> {}

#[define_opaque(Hidden)]
fn define(value: &dyn Sub, _: Hidden) {
    let _ = value as &dyn Super<()>;
}

fn main() {}

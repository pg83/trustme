// `Trait<K = 0>` binds an associated *const*, so the value after `=` is an
// expression, not a type. The parser only accepted a type there and rejected
// every such bound with "Unexpected token".
//
// NOTE: the equality itself is parsed and dropped — nothing checks it yet.
//
// Same shapes as the ui tests under associated-consts/.
#![feature(associated_const_equality)]

trait SuperSuperTrait<T> {
    const K: T;
}

trait SuperTrait: SuperSuperTrait<i32> {}

trait Trait: SuperTrait {}

struct Holder;

impl SuperSuperTrait<i32> for Holder {
    const K: i32 = 0;
}

impl SuperTrait for Holder {}

impl Trait for Holder {}

fn take(_: impl Trait<K = 0>) {}

trait Sized32 {
    const SIZE: usize;
}

impl Sized32 for u32 {
    const SIZE: usize = 4;
}

// A block-valued bound, and one on a where clause.
fn take_block<T>(_: T)
where
    T: Sized32<SIZE = { 4 }>,
{
}

fn main() {
    take(Holder);
    take_block(1u32);
}

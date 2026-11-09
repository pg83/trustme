//@ compile-fail: conflicting implementations
#![feature(type_alias_impl_trait)]

trait Produce {
    type Item;
}

struct Source;
impl Produce for Source {
    type Item = u8;
}

type Hidden = impl Produce;

#[define_opaque(Hidden)]
fn make() -> Hidden {
    Source
}

trait Bridge<T> {}
trait Choose<T> {
    fn choose(&self) -> u8;
}

struct Tag;
impl<T: Produce> Choose<T> for Tag
where
    u16: Bridge<T::Item>,
{
    fn choose(&self) -> u8 {
        11
    }
}

impl Choose<Hidden> for Tag {
    fn choose(&self) -> u8 {
        13
    }
}

fn main() {
    let _ = make();
    let _ = Tag.choose();
}

#![feature(type_alias_impl_trait, specialization, trivial_bounds)]
#![allow(incomplete_features)]

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
trait Choose {
    fn choose() -> u8;
}

struct Tag;
impl Choose for Tag {
    default fn choose() -> u8 {
        7
    }
}

impl Choose for Tag
where
    u16: Bridge<<Hidden as Produce>::Item>,
{
    fn choose() -> u8 {
        9
    }
}

fn main() {
    let _ = make();
    assert_eq!(Tag::choose(), 7);
}

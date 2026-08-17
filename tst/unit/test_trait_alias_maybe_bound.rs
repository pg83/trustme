// A trait alias may name a lifetime bound and no trait at all, in which case it
// adds nothing where it is used: the trait that follows it is the object's own.
#![feature(trait_alias)]

trait Draw {
    fn draw(&self) -> u32;
}

impl Draw for u32 {
    fn draw(&self) -> u32 {
        *self
    }
}

trait Static = 'static;
trait DrawAlias = Draw;
trait NestedAlias = DrawAlias;

type LeadingAlias = dyn Static + Draw;
type TrailingAlias = dyn Draw + Static;
type ThroughAliases = dyn NestedAlias + Send;
type WithAuto = dyn Draw + Static + Send;

fn leading(value: &LeadingAlias) -> u32 {
    value.draw()
}

fn trailing(value: &TrailingAlias) -> u32 {
    value.draw()
}

fn through(value: &ThroughAliases) -> u32 {
    value.draw()
}

fn with_auto(value: &WithAuto) -> u32 {
    value.draw()
}

fn main() {
    assert_eq!(leading(&1u32), 1);
    assert_eq!(trailing(&2u32), 2);
    assert_eq!(through(&3u32), 3);
    assert_eq!(with_auto(&4u32), 4);
}

//@ compile-fail: error:

trait Relates<R, Tag> {}
impl<T, Tag> Relates<T, Tag> for T {}

fn require<A, B, Tag>(_: A, _: B, tag: Tag) -> Tag
where A: Relates<B, Tag>
{
    tag
}

struct Source;
impl Source {
    fn front() -> impl Copy { 37u8 }
    fn back() -> impl Copy { 41u8 }
}

fn main() {
    let _ = require(Source::front(), Source::back(), 43);
}

//@ compile-fail: error:

trait Relates<R, Tag> {}
impl<T, Tag> Relates<T, Tag> for T {}

fn require<A, B, Tag>(_: A, _: B, tag: Tag) -> Tag
where A: Relates<B, Tag>
{
    tag
}

fn outside() -> impl Copy { 23u8 }
struct Source;
impl Source {
    fn inside() -> impl Copy { 29u8 }
}

fn main() {
    let _ = require(outside(), Source::inside(), 31);
}

//@ compile-fail: error:

trait Relates<R, Tag> {}
impl<T, Tag> Relates<T, Tag> for T {}

fn require<A, B, Tag>(_: A, _: B, tag: Tag) -> Tag
where A: Relates<B, Tag>
{
    tag
}

trait Factory {
    fn front() -> impl Copy;
    fn back() -> impl Copy;
}

struct Source;
impl Factory for Source {
    fn front() -> impl Copy { 47u8 }
    fn back() -> impl Copy { 53u8 }
}

fn main() {
    let _ = require(<Source as Factory>::front(), <Source as Factory>::back(), 59);
}

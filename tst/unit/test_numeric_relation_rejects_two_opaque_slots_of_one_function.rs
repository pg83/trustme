//@ compile-fail: error:

trait Relates<R, Tag> {}
impl<T, Tag> Relates<T, Tag> for T {}

fn require<A, B, Tag>(_: A, _: B, tag: Tag) -> Tag
where A: Relates<B, Tag>
{
    tag
}

fn values() -> (impl Copy, impl Copy) { (179u8, 181u8) }

fn main() {
    let (first, second) = values();
    let _ = require(first, second, 191);
}

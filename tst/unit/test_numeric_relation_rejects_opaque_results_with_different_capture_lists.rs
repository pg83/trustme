//@ compile-fail: error:

trait Relates<R, Tag> {}
impl<T, Tag> Relates<T, Tag> for T {}

fn require<A, B, Tag>(_: A, _: B, tag: Tag) -> Tag
where A: Relates<B, Tag>
{
    tag
}

fn single<T: Copy>(value: T) -> impl Copy + use<T> { value }
fn paired<T: Copy, U: Copy>(left: T, right: U) -> impl Copy + use<T, U> {
    (left, right)
}

fn main() {
    let _ = require(single(113u8), paired(127u8, 131u16), 137);
}

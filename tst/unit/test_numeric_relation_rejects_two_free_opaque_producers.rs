//@ compile-fail: error:

trait Relates<R, Tag> {}
impl<T, Tag> Relates<T, Tag> for T {}

fn require<A, B, Tag>(_: A, _: B, tag: Tag) -> Tag
where A: Relates<B, Tag>
{
    tag
}

fn north() -> impl Copy { 13u8 }
fn south() -> impl Copy { 17u8 }

fn main() {
    let _ = require(north(), south(), 19);
}

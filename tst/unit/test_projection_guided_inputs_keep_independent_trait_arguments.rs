trait Source { type Item; }
impl<T> Source for Option<T> { type Item = T; }

trait Fits<T> {}
impl Fits<u16> for u8 {}
impl Fits<u32> for u8 {}

struct Packet<S>(S);
trait Checked { type Item; }
impl<S, T> Checked for Packet<S>
where
    S: Source<Item = T>,
    u8: Fits<T>,
{
    type Item = T;
}

fn select<S: Checked>(_: S, value: S::Item) -> S::Item { value }

fn main() {
    let mut narrow = None;
    let first = select(Packet(narrow), 23u16);
    narrow = Some(19u16);
    assert_eq!((first, narrow.unwrap()), (23, 19));

    let mut wide = None;
    let second = select(Packet(wide), 37u32);
    wide = Some(29u32);
    assert_eq!((second, wide.unwrap()), (37, 29));
}

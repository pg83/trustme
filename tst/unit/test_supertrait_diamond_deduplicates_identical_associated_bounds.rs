trait Source { type Item; }
trait Left: Source<Item: Copy + Eq> {}
trait Right: Source<Item: Eq + Copy> {}
trait Again: Source<Item: Copy + Eq> {}
trait Both: Left + Right + Again {}
struct Value;
impl Source for Value { type Item = u16; }
impl Left for Value {}
impl Right for Value {}
impl Again for Value {}
impl Both for Value {}

fn copy<I: Both>(value: I::Item) -> (I::Item, I::Item) {
    assert!(value == value);
    (value, value)
}

fn main() {
    assert_eq!(copy::<Value>(42), (42, 42));
}

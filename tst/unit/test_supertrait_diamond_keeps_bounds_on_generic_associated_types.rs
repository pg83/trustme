trait Source { type Cell<T, const N: usize>; }
trait Left: Source<Cell<u16, 3>: Copy> {}
trait Right: Source<Cell<u16, 3>: Eq> {}
trait Both: Left + Right {}
struct Value;
impl Source for Value { type Cell<T, const N: usize> = u16; }
impl Left for Value {}
impl Right for Value {}
impl Both for Value {}

fn duplicate<I: Both>(value: I::Cell<u16, 3>) -> (I::Cell<u16, 3>, I::Cell<u16, 3>) {
    assert!(value == value);
    (value, value)
}

fn main() {
    assert_eq!(duplicate::<Value>(42), (42, 42));
}

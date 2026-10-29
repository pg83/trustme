type Twice<T> = (T, T);

trait Repeat {
    fn repeat<T: Copy + PartialEq>(value: T) -> Twice<impl Copy + PartialEq>;
}

struct Source;
impl Repeat for Source {
    fn repeat<T: Copy + PartialEq>(value: T) -> Twice<impl Copy + PartialEq> {
        (Some(value), Some(value))
    }
}

fn nested<T: Copy + PartialEq>(value: T) -> impl Iterator<Item = impl Copy + PartialEq> {
    [value, value].into_iter()
}

fn main() {
    let (first, second) = Source::repeat(23u16);
    assert!(first == second);
    let (first, second) = Source::repeat([4u8; 3]);
    assert!(first == second);
    let mut values = nested(12u32);
    assert!(values.next() == values.next());
}

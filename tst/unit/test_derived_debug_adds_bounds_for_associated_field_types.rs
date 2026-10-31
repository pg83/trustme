trait Family { type Item; }
#[derive(Debug)]
struct Source;
impl Family for Source { type Item = u16; }
#[derive(Debug)]
struct Record<T: Family> { item: T::Item, nested: Option<(T::Item,)> }
fn main() {
    let record = Record::<Source> { item: 37, nested: Some((41,)) };
    assert_eq!(format!("{:?}", record), "Record { item: 37, nested: Some((41,)) }");
}

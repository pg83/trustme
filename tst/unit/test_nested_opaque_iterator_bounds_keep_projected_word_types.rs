trait Family { type Word: Copy; }
struct Whole;
impl Family for Whole { type Word = u32; }
fn rows<F: Family>(value: F::Word) -> impl Iterator<Item: IntoIterator<Item = F::Word>> {
    std::iter::once([value; 2])
}
fn main() {
    let values = rows::<Whole>(283).flatten().collect::<Vec<_>>();
    assert_eq!(values, [283, 283]);
}

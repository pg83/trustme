trait Family { type Word: Copy; }
struct Whole;
impl Family for Whole { type Word = u32; }
fn free<F: Family>(value: F::Word) -> impl Iterator<Item = F::Word> { std::iter::once(value) }
struct Factory<F: Family>(F::Word);
impl<F: Family> Factory<F> {
    fn inherent(&self) -> impl Iterator<Item = F::Word> { std::iter::once(self.0) }
}
trait Produce: Family {
    fn produce(value: Self::Word) -> impl Iterator<Item = Self::Word> { std::iter::once(value) }
}
impl Produce for Whole {}
fn main() {
    assert_eq!(free::<Whole>(151).next(), Some(151));
    assert_eq!(Factory::<Whole>(157).inherent().next(), Some(157));
    assert_eq!(Whole::produce(163).next(), Some(163));
}

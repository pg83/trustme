trait Family { type Word: Copy; }
struct Whole;
impl Family for Whole { type Word = u32; }
fn free<T>(value: T) -> T { value }
struct Wrapper<T>(T);
impl<T> Wrapper<T> { fn inherent(value: T) -> T { value } }
trait Identity<T> { fn identity(value: T) -> T { value } }
impl<T> Identity<T> for Wrapper<T> {}
fn exercise<F: Family>(value: F::Word) -> (F::Word, F::Word, F::Word) {
    let first = free::<F::Word>;
    let second = Wrapper::<F::Word>::inherent;
    let third = <Wrapper<F::Word> as Identity<F::Word>>::identity;
    (first(value), second(value), third(value))
}
fn main() { assert_eq!(exercise::<Whole>(179), (179, 179, 179)); }

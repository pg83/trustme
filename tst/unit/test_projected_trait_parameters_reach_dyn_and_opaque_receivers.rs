trait Family { type Word: Copy; }
struct Whole;
impl Family for Whole { type Word = u32; }
trait Echo<T> { fn give(&self, value: T) -> T; }
struct Pulse;
impl<T> Echo<T> for Pulse { fn give(&self, value: T) -> T { value } }
fn from_family<F: Family>() -> impl Echo<F::Word> { Pulse }
fn from_word<T>() -> impl Echo<T> { Pulse }
fn exercise<F: Family>(object: &dyn Echo<F::Word>, value: F::Word) -> (F::Word, F::Word, F::Word) {
    let family = from_family::<F>();
    let word = from_word::<F::Word>();
    (object.give(value), family.give(value), word.give(value))
}
fn main() { assert_eq!(exercise::<Whole>(&Pulse, 359), (359, 359, 359)); }

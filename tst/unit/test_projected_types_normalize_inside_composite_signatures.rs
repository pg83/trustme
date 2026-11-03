trait Family { type Word: Copy; }
struct Whole;
impl Family for Whole { type Word = u32; }
fn pass<F: Family>(input: (&[F::Word], *const F::Word, fn(F::Word) -> F::Word, [F::Word; 2])) -> (F::Word, F::Word) {
    let (slice, pointer, callback, array) = input;
    assert_eq!(slice.len(), 2);
    (callback(unsafe { *pointer }), array[1])
}
fn from_iter<F: Family>(values: &mut (dyn Iterator<Item = F::Word> + Send)) -> Option<F::Word> {
    values.next()
}
fn main() {
    let values = [127u32, 131];
    assert_eq!(pass::<Whole>((&values, values.as_ptr(), |v| v + 2, values)), (129, 131));
    assert_eq!(from_iter::<Whole>(&mut std::iter::once(137)), Some(137));
}

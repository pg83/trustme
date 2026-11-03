trait Family { type Word: Copy; }
struct Whole;
impl Family for Whole { type Word = u32; }
struct Selector;
trait Convert<T> { fn same(value: T) -> T { value } fn with<U>(value: T, extra: U) -> (T, U) { (value, extra) } }
impl<T> Convert<T> for Selector {}
fn exercise<F: Family>(value: F::Word) -> (F::Word, (F::Word, u16)) {
    let same = <Selector as Convert<F::Word>>::same;
    let paired = <Selector as Convert<F::Word>>::with::<u16>;
    (same(value), paired(value, 293))
}
fn main() { assert_eq!(exercise::<Whole>(307), (307, (307, 293))); }

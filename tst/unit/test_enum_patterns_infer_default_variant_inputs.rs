enum Choice<T> { Pair(T, T), Empty }
impl Default for Choice<u16> {
    fn default() -> Self { Self::Pair(29, 31) }
}
fn main() {
    let sum = match Default::default() {
        Choice::Pair(first, second) => first + second,
        Choice::Empty => 0,
    };
    assert_eq!(sum, 60u16);
    assert!(matches!(Choice::<u16>::Empty, Choice::Empty));
}

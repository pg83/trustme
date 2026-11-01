fn main() {
    type Word = u16;
    static NUMBER: Word = 53;
    trait Twice { fn twice(self) -> Self; }
    impl Twice for Word { fn twice(self) -> Self { self * 2 } }
    use Word as Value;
    use NUMBER as SAVED;
    use Twice as Rule;
    fn apply<T: Rule>(value: T) -> T { value.twice() }
    let value: Value = SAVED;
    assert_eq!(apply(value), 106);
}

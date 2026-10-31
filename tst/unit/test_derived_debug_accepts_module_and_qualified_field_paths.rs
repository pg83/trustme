mod values {
    pub type Number = u16;
    pub trait Family { type Item; }
    pub struct Source;
    impl Family for Source { type Item = Number; }
    #[derive(Debug)]
    pub struct Record {
        pub local: self::Number,
        pub parent: super::Number,
        pub absolute: ::std::primitive::u16,
        pub qualified: <Source as Family>::Item,
    }
}
type Number = u16;
fn main() {
    let value = values::Record { local: 3, parent: 5, absolute: 7, qualified: 11 };
    assert_eq!(format!("{:?}", value), "Record { local: 3, parent: 5, absolute: 7, qualified: 11 }");
}

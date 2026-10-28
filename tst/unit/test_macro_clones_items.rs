#![feature(auto_traits, negative_impls)]

macro_rules! duplicate {
    ($left:ident, $right:ident, $($item:item)*) => {
        mod $left { $($item)* }
        mod $right { $($item)* }
    };
}

duplicate!(first, second,
    pub const BASE: usize = 3;
    pub static TABLE: [u8; BASE] = [2, 4, 6];
    pub type Array<const N: usize> = [u8; N];
    pub type Callback = for<'a> fn(&'a u8) -> &'a u8;
    pub struct Named<T> { pub value: T }
    pub struct Tuple<T>(pub T);
    pub struct Unit;
    pub enum Choice<T> { Empty, Tuple(T), Named { value: T } }
    pub union Bits { pub byte: u8, pub word: u32 }
    pub auto trait Marker {}
    impl !Marker for Unit {}
    pub trait Read {
        type Value;
        const WIDTH: usize = 1;
        fn read(&self) -> Self::Value;
    }
    impl Read for Named<u8> {
        type Value = u8;
        const WIDTH: usize = 2;
        fn read(&self) -> u8 { self.value }
    }
    macro_rules! accessor { () => { pub fn get(&self) -> u8 { self.0 } } }
    impl Tuple<u8> { accessor!(); }
    extern "C" { fn abs(value: i32) -> i32; }
    pub mod nested {
        pub mod deeper { pub fn value() -> u8 { 7 } }
        pub fn value() -> u8 { deeper::value() }
    }
    pub fn identity(value: &u8) -> &u8 { value }
    pub fn sum<const N: usize>(values: Array<N>) -> u8 {
        let mut sum = 0;
        for value in values { sum += value; }
        sum
    }
    pub fn run() -> u8 {
        let callback: Callback = identity;
        let array: Array<3> = TABLE;
        let named = Named { value: 5u8 };
        let tuple = Tuple(6u8);
        assert_eq!(tuple.get(), 6);
        assert_eq!(<Named<u8> as Read>::WIDTH, 2);
        assert_eq!(unsafe { abs(-11) }, 11);
        let _unit = Unit;
        let _empty = Choice::<u8>::Empty;
        let pair = Choice::Tuple(tuple.0);
        let field = Choice::Named { value: named.read() };
        let Choice::Tuple(a) = pair else { panic!() };
        let Choice::Named { value: b } = field else { panic!() };
        let bits = Bits { word: 0 };
        sum(array) + *callback(&a) + b + nested::value() + unsafe { bits.byte }
    }
);

fn main() {
    assert_eq!(first::run(), 30);
    assert_eq!(second::run(), 30);
}

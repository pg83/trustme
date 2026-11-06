mod outer {
    pub const LOW: u8 = 2;
    pub struct Value;
    pub trait Read { fn read() -> u8; }
    impl Read for Value { fn read() -> u8 { 17 } }
    impl Value {
        const HIGH: u8 = 3;
        pub fn classify(value: u8) -> u8 {
            match value {
                self::LOW => 5,
                Self::HIGH => 7,
                _ => 11,
            }
        }
        pub fn construct() -> Self { Self }
    }
    pub mod inner {
        pub fn read() -> u8 { super::LOW }
    }
}
fn generic<T: outer::Read>() -> u8 { T::read() }
fn main() {
    let function = |value: u8| value + 1;
    assert_eq!(function(4), 5);
    let _: outer::Value = outer::Value::construct();
    assert_eq!(outer::Value::classify(2), 5);
    assert_eq!(outer::Value::classify(3), 7);
    assert_eq!(outer::Value::classify(4), 11);
    assert_eq!(outer::inner::read(), 2);
    assert_eq!(<outer::Value as outer::Read>::read(), 17);
    assert_eq!(generic::<outer::Value>(), 17);
}

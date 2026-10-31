#![feature(try_blocks)]
macro_rules! keep { ($value:expr) => { $value }; }
macro_rules! value { () => { 17u8 }; }
struct Number(u32);
impl Number {
    fn count<T>(&self) -> u32 { self.0 + std::mem::size_of::<T>() as u32 }
}
fn main() {
    let add = keep!(|value: u32| -> u32 { value + 3 });
    let increment = 5;
    let captured = keep!(move |value: u32| value + increment);
    assert_eq!((add(17), captured(23)), (20, 28));
    let future = keep!(async move { 37u8 });
    drop(future);
    let attempt: Result<u16, ()> = keep!(try { 41u16 });
    assert_eq!(attempt, Ok(41));
    assert_eq!(keep!(Number { 0: 11 }.count::<u16>()), 13);
    assert_eq!(keep!(value!{}), 17);
    assert_eq!(keep!((19u8, 23u8)).1, 23);
    assert_eq!(keep!([17u8, 37])[1], 37);
}

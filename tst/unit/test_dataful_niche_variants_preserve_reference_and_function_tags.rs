enum Borrowed<'a> { Count(u16), Data(&'a u32, [u8; 16]) }
enum Callback { Count(u16), Data(fn() -> u16, [u8; 16]) }
fn callback() -> u16 { 37 }
#[inline(never)]
fn borrowed(value: Borrowed<'_>) -> u32 {
    match value { Borrowed::Count(n) => n as u32, Borrowed::Data(n, bytes) => *n + bytes[15] as u32 }
}
#[inline(never)]
fn called(value: Callback) -> u16 {
    match value { Callback::Count(n) => n, Callback::Data(f, bytes) => f() + bytes[15] as u16 }
}
fn main() {
    let number = 30;
    let mut bytes = [0u8; 16]; bytes[15] = 7;
    assert_eq!(borrowed(Borrowed::Count(41)), 41);
    assert_eq!(borrowed(Borrowed::Data(&number, bytes)), 37);
    assert_eq!(called(Callback::Count(43)), 43);
    assert_eq!(called(Callback::Data(callback, bytes)), 44);
}

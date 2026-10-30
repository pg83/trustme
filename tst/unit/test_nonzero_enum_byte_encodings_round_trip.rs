use std::num::NonZeroU8;
#[derive(Clone, Copy)]
enum Nullable { Empty, Value(NonZeroU8) }
fn main() {
    assert_eq!(std::mem::size_of::<Nullable>(), 1);
    for raw in 0..=255u8 {
        let value: Nullable = unsafe { std::mem::transmute(raw) };
        let decoded = match value { Nullable::Empty => 0, Nullable::Value(n) => n.get() };
        assert_eq!(decoded, raw);
        let value: Option<NonZeroU8> = unsafe { std::mem::transmute(raw) };
        assert_eq!(value.map(NonZeroU8::get).unwrap_or(0), raw);
        let encoded: u8 = unsafe { std::mem::transmute(value) };
        assert_eq!(encoded, raw);
    }
}

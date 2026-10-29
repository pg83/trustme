#![feature(arbitrary_self_types_pointers)]
struct Counter(u32);
impl Counter {
    unsafe fn peek_value(self: *const Self) -> u32 { (*self).0 }
    unsafe fn bump(self: *mut Self, amount: u32) { (*self).0 += amount; }
}
fn main() {
    let mut value = Counter(5);
    let pointer = &mut value as *mut Counter;
    unsafe {
        pointer.bump(7);
        assert_eq!((pointer as *const Counter).peek_value(), 12);
    }
}

trait Counter {
    fn read(&self, add: u8) -> u8;
    fn bump(&mut self, add: u8);
    fn take(self: Box<Self>) -> u8;
}
impl Counter for u8 {
    fn read(&self, add: u8) -> u8 { *self + add }
    fn bump(&mut self, add: u8) { *self += add; }
    fn take(self: Box<Self>) -> u8 { *self }
}
fn main() {
    let read: fn(&(dyn Counter + 'static), u8) -> u8 = <dyn Counter as Counter>::read;
    let read = std::hint::black_box(read);
    let bump: fn(&mut (dyn Counter + 'static), u8) = <dyn Counter as Counter>::bump;
    let bump = std::hint::black_box(bump);
    let take: fn(Box<dyn Counter>) -> u8 = <dyn Counter as Counter>::take;
    let take = std::hint::black_box(take);
    let mut number = 7u8;
    bump(&mut number, 4);
    assert_eq!(read(&number, 2), 13);
    assert_eq!(take(Box::new(23u8)), 23);
}

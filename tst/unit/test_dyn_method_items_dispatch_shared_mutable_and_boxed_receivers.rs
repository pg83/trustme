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
    let read = <dyn Counter as Counter>::read;
    let bump = <dyn Counter as Counter>::bump;
    let take = <dyn Counter as Counter>::take;
    let mut number = 7u8;
    bump(&mut number, 4);
    assert_eq!(read(&number, 2), 13);
    assert_eq!(take(Box::new(23u8)), 23);
}

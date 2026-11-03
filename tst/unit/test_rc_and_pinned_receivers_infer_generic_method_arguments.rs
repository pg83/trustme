use std::pin::Pin;
use std::rc::Rc;
struct Sensor<T>(T);
impl<T: Copy> Sensor<T> {
    fn paired<U: Copy>(self: Rc<Self>, other: U) -> (T, U) { (self.0, other) }
    fn pinned(self: Pin<&Self>) -> T { self.0 }
}
fn main() {
    let sensor = Rc::new(Sensor(101u16));
    assert_eq!(sensor.clone().paired(103u32), (101, 103));
    assert_eq!(sensor.clone().paired([7u8, 11]), (101, [7, 11]));
    assert_eq!(Pin::new(&*sensor).pinned(), 101);
}

use std::ops::Deref;

struct Shell<T>(T);
impl<T> Deref for Shell<T> {
    type Target = T;
    fn deref(&self) -> &T { &self.0 }
}

fn read(value: &Box<u16>) -> u16 { **value }
fn produce<T, F: FnOnce() -> T>(make: F) -> Shell<T> { Shell(make()) }

fn main() {
    assert_eq!(read(&Shell(std::convert::identity(Box::new(23u16)))), 23);
    assert_eq!(read(&produce(|| Box::new(31u16))), 31);
    let choose = |enabled| if enabled { Box::new(47u16) } else { Box::new(53u16) };
    assert_eq!(read(&Shell(choose(false))), 53);
}

//@ compile-variant: -O
struct Local(u8);
trait Read { fn read(&self) -> u8; }
impl Read for (Local, Local) { fn read(&self) -> u8 { self.0.0 + self.1.0 } }
impl Read for [Local; 2] { fn read(&self) -> u8 { self[0].0 + self[1].0 } }
impl Read for &Local { fn read(&self) -> u8 { self.0 } }
fn main() {
    assert_eq!((Local(3), Local(5)).read(), 8);
    assert_eq!([Local(7), Local(11)].read(), 18);
    assert_eq!(<&Local as Read>::read(&&Local(13)), 13);
}

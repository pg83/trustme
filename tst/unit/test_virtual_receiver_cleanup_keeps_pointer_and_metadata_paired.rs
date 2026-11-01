trait Read { fn read(&self, index: usize) -> u16; }
trait Child: Read { fn bonus(&self) -> u16; }
impl Read for [u16; 3] { fn read(&self, index: usize) -> u16 { self[index] } }
impl Child for [u16; 3] { fn bonus(&self) -> u16 { self[0] + self[2] } }

fn read<T: Read + ?Sized>(value: &T, index: usize) -> u16 { value.read(index) }

trait Consume { fn consume(self: Box<Self>) -> u16; }
struct Pair(u16, u16);
impl Consume for Pair { fn consume(self: Box<Self>) -> u16 { self.0 + self.1 } }

fn main() {
    let value = [3, 5, 7];
    let child: &dyn Child = &value;
    let parent: &dyn Read = child;
    assert_eq!(read(parent, 1), 5);
    assert_eq!(read(child, 2), 7);
    assert_eq!(child.bonus(), 10);
    let owned: Box<dyn Consume> = Box::new(Pair(11, 13));
    assert_eq!(owned.consume(), 24);
}

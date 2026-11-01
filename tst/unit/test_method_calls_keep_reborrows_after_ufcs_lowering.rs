struct Counter(u16);
impl Counter {
    const fn get(&self) -> u16 { self.0 }
}
trait Add { fn add(&mut self, value: &mut u16); }
impl Add for Counter {
    fn add(&mut self, value: &mut u16) {
        self.0 += *value;
        *value += 1;
    }
}

fn twice<C: Add>(counter: &mut C, value: &mut u16) {
    counter.add(value);
    counter.add(value);
}

const N: usize = Counter(3).get() as usize;
#[repr(u8)]
enum Packet {
    Empty = Counter(4).get() as u8,
    Data(u8) = Counter(5).get() as u8,
}

fn main() {
    let mut counter = Counter(2);
    let mut value = 3;
    twice(&mut counter, &mut value);
    assert_eq!(counter.get(), 9);
    assert_eq!(value, 5);
    let array: [u8; N] = [1, 2, 3];
    assert_eq!(array.len(), 3);
    assert!(matches!(Packet::Empty, Packet::Empty));
    assert_eq!(match Packet::Data(7) { Packet::Data(value) => value, _ => 0 }, 7);
}

use std::cell::Cell;
struct Pair { left: u32, right: u32 }
fn index(log: &Cell<u32>, digit: u32, position: usize) -> usize { log.set(log.get() * 10 + digit); position }
fn values(log: &Cell<u32>) -> Pair { log.set(log.get() * 10 + 1); Pair { left: 17, right: 23 } }
fn main() {
    let log = Cell::new(0);
    let mut output = [0; 2];
    Pair { left: output[index(&log, 2, 0)], right: output[index(&log, 3, 1)] } = values(&log);
    assert_eq!((output, log.get()), ([17, 23], 123));
}

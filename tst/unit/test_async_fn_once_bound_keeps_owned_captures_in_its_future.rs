use std::cell::Cell;
use std::future::Future;
use std::rc::Rc;

struct Token { value: u16, drops: Rc<Cell<usize>> }
impl Token {
    fn read(&self) -> u16 { self.value }
    fn bump(&mut self) -> u16 { self.value += 1; self.value }
}
impl Drop for Token {
    fn drop(&mut self) { self.drops.set(self.drops.get() + 1); }
}

fn take<F, R>(call: F) -> R
where F: FnOnce() -> R, R: Future<Output = u16> { call() }

fn main() {
    let drops = Rc::new(Cell::new(0));
    let first = Token { value: 257, drops: drops.clone() };
    let future = take(async move || first.read());
    assert_eq!(drops.get(), 0);
    drop(future);
    assert_eq!(drops.get(), 1);

    let mut second = Token { value: 263, drops: drops.clone() };
    let future = take(async move || second.bump());
    assert_eq!(drops.get(), 1);
    drop(future);
    assert_eq!(drops.get(), 2);
}

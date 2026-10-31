use std::cell::Cell;
use std::future::Future;
use std::rc::Rc;

struct Token { id: u16, drops: Rc<Cell<usize>> }
impl Token { fn read(&self) -> u16 { self.id } }
impl Drop for Token {
    fn drop(&mut self) { self.drops.set(self.drops.get() + 1); }
}

fn take<F: AsyncFnOnce() -> u16>(call: F) -> impl Future<Output = u16> {
    call()
}

fn main() {
    let drops = Rc::new(Cell::new(0));
    let token = Token { id: 257, drops: drops.clone() };
    let call = async move || token.read();
    let future = take(call);
    assert_eq!(drops.get(), 0);
    drop(future);
    assert_eq!(drops.get(), 1);
}

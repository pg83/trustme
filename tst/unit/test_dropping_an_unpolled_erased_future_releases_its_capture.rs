use std::cell::Cell;
use std::future::Future;
use std::pin::Pin;
struct Leaf<'a>(&'a Cell<u32>);
impl Drop for Leaf<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
fn main() {
    let log = Cell::new(0);
    let leaf = Leaf(&log);
    let future: Pin<Box<dyn Future<Output = ()> + '_>> = Box::pin(async move { drop(leaf); });
    drop(future);
    assert_eq!(log.get(), 1);
}

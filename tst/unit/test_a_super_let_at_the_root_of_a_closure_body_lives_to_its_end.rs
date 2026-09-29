//@ run-pass
// tokio-stream's tests poll `poll_fn(|cx| pin!(stream_map.next_many(..)).poll(cx))`,
// and `pin!` is `{ super let mut pinned = $value; .. }`. A `super let` takes
// the scope its enclosing block's value would extend to; a closure body that
// is no block of its own leaves that the body itself, as it is for any
// temporary of a function body. The lowering had no scope to give it there.
use std::pin::pin;

struct Noisy<'a>(u32, &'a std::cell::Cell<u32>);

impl Drop for Noisy<'_> {
    fn drop(&mut self) {
        self.1.set(self.1.get() + self.0);
    }
}

fn main() {
    let read = || *pin!(5u32);
    assert_eq!(read(), 5);

    let dropped = std::cell::Cell::new(0);
    let observe = |n: u32| pin!(Noisy(n, &dropped)).0 + dropped.get();
    assert_eq!(observe(7), 7);
    assert_eq!(dropped.get(), 7);
}

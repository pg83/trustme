// A `let` is a statement, the last one of a block too: the temporaries of
// its initializer that are not extended are dropped at the end of the
// statement, before the block's variables. mockall's `do_checkpoint` is
// `let timeses = get_expectations().lock().unwrap().checkpoint()
// .collect::<Vec<_>>();` with nothing after it; dropping `timeses` may panic,
// and the expectations' mutex must not be held by then.
use std::sync::Mutex;

struct Expectations(Vec<u8>);

impl Expectations {
    fn checkpoint(&mut self) -> std::vec::Drain<'_, u8> {
        self.0.drain(..)
    }
}

static ITEMS: Mutex<Expectations> = Mutex::new(Expectations(Vec::new()));

struct Checked;

impl Drop for Checked {
    fn drop(&mut self) {
        assert!(ITEMS.try_lock().is_ok(), "the guard of the `let` is still held");
    }
}

fn do_checkpoint() {
    let _timeses = ITEMS.lock().unwrap().checkpoint().map(|_| Checked).collect::<Vec<_>>();
}

fn main() {
    ITEMS.lock().unwrap().0.extend([1, 2, 3]);
    do_checkpoint();
    ITEMS.lock().unwrap().0.extend([4, 5]);
    let closure = || {
        let _taken = ITEMS.lock().unwrap().checkpoint().map(|_| Checked).collect::<Vec<_>>();
    };
    closure();
    assert!(ITEMS.lock().unwrap().0.is_empty());
}

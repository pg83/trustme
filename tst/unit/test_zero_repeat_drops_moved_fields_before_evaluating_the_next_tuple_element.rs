use std::sync::atomic::{AtomicUsize, Ordering};

static DROPS: AtomicUsize = AtomicUsize::new(0);
struct Token(usize);
impl Drop for Token {
    fn drop(&mut self) {
        DROPS.fetch_add(self.0, Ordering::SeqCst);
    }
}
struct Pair { first: Token, second: Token }

#[inline(never)]
fn count() -> usize { DROPS.load(Ordering::SeqCst) }

fn main() {
    let pair = Pair { first: Token(3), second: Token(7) };
    let observation = ([pair.first; 0], count());
    assert_eq!(observation.1, 3);
    drop(pair.second);
    assert_eq!(count(), 10);
    let mut assigned = [Token(1); 0];
    assert_eq!(count(), 11);
    assigned = [Token(2); 0];
    assert_eq!(count(), 13);
    std::hint::black_box(assigned);
    assert_eq!(count(), 13);
    let single = [Token(5); 1];
    assert_eq!(count(), 13);
    drop(single);
    assert_eq!(count(), 18);
    let nested = [([Token(11); 0], count()); 1];
    assert_eq!(nested[0].1, 29);
    drop(nested);
    assert_eq!(count(), 29);
}

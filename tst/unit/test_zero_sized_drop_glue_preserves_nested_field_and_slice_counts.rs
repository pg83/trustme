use std::sync::atomic::{AtomicUsize, Ordering};

static DROPS: AtomicUsize = AtomicUsize::new(0);
struct Z;
impl Drop for Z {
    #[inline(never)]
    fn drop(&mut self) { DROPS.fetch_add(1, Ordering::SeqCst); }
}
struct Nested { prefix: u8, tail: ((Z,), Z) }
enum Choice { Empty, Full((Z, Z)) }

#[inline(never)]
fn returned() -> Z { Z }

fn main() {
    drop(returned());
    drop([Z, Z, Z]);
    let nested = Nested { prefix: 17, tail: ((Z,), Z) };
    assert_eq!(nested.prefix, 17);
    drop(nested);
    drop(Choice::Empty);
    drop(Choice::Full((Z, Z)));
    let slice: Box<[Z]> = Box::new([Z, Z, Z, Z]);
    drop(slice);
    assert_eq!(DROPS.load(Ordering::SeqCst), 12);
}

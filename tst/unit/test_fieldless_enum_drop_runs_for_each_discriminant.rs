use std::sync::atomic::{AtomicU32, Ordering};
static LOG: AtomicU32 = AtomicU32::new(0);
#[repr(u8)]
enum Tag { First = 1, Second = 2 }
impl Drop for Tag {
    fn drop(&mut self) {
        let digit = match self { Tag::First => 1, Tag::Second => 2 };
        LOG.store(LOG.load(Ordering::Relaxed) * 10 + digit, Ordering::Relaxed);
    }
}
fn main() { drop((Tag::First, Tag::Second)); assert_eq!(LOG.load(Ordering::Relaxed), 12); }

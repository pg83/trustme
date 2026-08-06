// Extracted from library/std/src/sync/once_lock.rs:53
#![allow(unused)]
fn main() {
    use std::sync::{OnceLock, atomic::{AtomicU32, Ordering}};
    use std::thread;
    
    struct OnceList<T> {
        data: OnceLock<T>,
        next: OnceLock<Box<OnceList<T>>>,
    }
    impl<T> OnceList<T> {
        const fn new() -> OnceList<T> {
            OnceList { data: OnceLock::new(), next: OnceLock::new() }
        }
        fn push(&self, value: T) {
            // FIXME: this impl is concise, but is also slow for long lists or many threads.
            // as an exercise, consider how you might improve on it while preserving the behavior
            if let Err(value) = self.data.set(value) {
                let next = self.next.get_or_init(|| Box::new(OnceList::new()));
                next.push(value)
            };
        }
        fn contains(&self, example: &T) -> bool
        where
            T: PartialEq,
        {
            self.data.get().map(|item| item == example).filter(|v| *v).unwrap_or_else(|| {
                self.next.get().map(|next| next.contains(example)).unwrap_or(false)
            })
        }
    }
    
    // Let's exercise this new Sync append-only list by doing a little counting
    static LIST: OnceList<u32> = OnceList::new();
    static COUNTER: AtomicU32 = AtomicU32::new(0);
    
    const LEN: u32 = if cfg!(miri) { 50 } else { 1000 };
    /*
    const LEN: u32 = 1000;
    */
    thread::scope(|s| {
        for _ in 0..thread::available_parallelism().unwrap().get() {
            s.spawn(|| {
                while let i @ 0..LEN = COUNTER.fetch_add(1, Ordering::Relaxed) {
                    LIST.push(i);
                }
            });
        }
    });
    
    for i in 0..LEN {
        assert!(LIST.contains(&i));
    }
}

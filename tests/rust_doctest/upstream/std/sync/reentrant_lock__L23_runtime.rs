// Extracted from library/std/src/sync/reentrant_lock.rs:23
#![allow(unused)]
#![feature(reentrant_lock)]
fn main() {
    
    use std::cell::RefCell;
    use std::sync::ReentrantLock;
    
    pub struct Log {
        data: RefCell<String>,
    }
    
    impl Log {
        pub fn append(&self, msg: &str) {
            self.data.borrow_mut().push_str(msg);
        }
    }
    
    static LOG: ReentrantLock<Log> = ReentrantLock::new(Log { data: RefCell::new(String::new()) });
    
    pub fn with_log<R>(f: impl FnOnce(&Log) -> R) -> R {
        let log = LOG.lock();
        f(&*log)
    }
    
    with_log(|log| {
        log.append("Hello");
        with_log(|log| log.append(" there!"));
    });
}

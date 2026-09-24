//@ edition: 2021
// tokio's batch semaphore `poll_acquire` declares `let mut lock = None;`,
// takes the waiters lock inside a retry loop (`lock = Some(..)`), and may
// `return` from a later iteration: the guard must be dropped there. rustc
// computes a local's initialisation at a loop head over every predecessor,
// the back edges included. We took the state before the loop: the second
// iteration believed `lock` was still `None` (and a late-initialised local
// still uninitialised), so the guard leaked and every worker parked on the
// waiters mutex.
#![allow(unused_assignments, unused_variables)]
use std::cell::RefCell;

struct Log<'a>(u32, &'a RefCell<Vec<u32>>);
impl Drop for Log<'_> {
    fn drop(&mut self) {
        self.1.borrow_mut().push(self.0);
    }
}

fn assigned_then_returned(log: &RefCell<Vec<u32>>) -> u32 {
    let mut lock = None;
    let mut tries = 0;
    loop {
        if lock.is_none() {
            lock = Some(Log(1, log));
        }
        tries += 1;
        if tries <= 1 {
            continue;
        }
        return tries;
    }
}

fn initialised_late(log: &RefCell<Vec<u32>>) {
    let mut x;
    let mut i = 0;
    loop {
        i += 1;
        if i == 1 || i == 3 {
            x = Log(10 + i, log);
        }
        if i == 4 {
            break;
        }
    }
}

fn main() {
    let log = RefCell::new(Vec::new());
    assert_eq!(assigned_then_returned(&log), 2);
    assert_eq!(*log.borrow(), [1]);
    log.borrow_mut().clear();
    initialised_late(&log);
    assert_eq!(*log.borrow(), [11, 13]);
}

/* parking_lot_core `grow_hashtable`: the loop breaks with `table: &HashTable` after
   comparing `table as *const _ as *mut _` with the atomic's load; `old_table` is the
   reference, and `old_table.entries` is its field. */
use std::ptr;
use std::sync::atomic::{AtomicPtr, Ordering};

struct HashTable {
    entries: Box<[u32]>,
}

static HASHTABLE: AtomicPtr<HashTable> = AtomicPtr::new(ptr::null_mut());

fn get_hashtable() -> &'static HashTable {
    let table = HASHTABLE.load(Ordering::Acquire);
    if !table.is_null() {
        return unsafe { &*table };
    }
    let new = Box::into_raw(Box::new(HashTable { entries: vec![1, 2, 3].into_boxed_slice() }));
    match HASHTABLE.compare_exchange(ptr::null_mut(), new, Ordering::AcqRel, Ordering::Acquire) {
        Ok(_) => unsafe { &*new },
        Err(old) => {
            drop(unsafe { Box::from_raw(new) });
            unsafe { &*old }
        }
    }
}

fn new_table(_num_threads: usize, prev: *const HashTable) -> usize {
    unsafe { (&(*prev).entries).len() }
}

fn grow_hashtable(num_threads: usize) -> usize {
    let old_table = loop {
        let table = get_hashtable();
        if table.entries.len() >= 8 * num_threads {
            return 0;
        }
        if HASHTABLE.load(Ordering::Relaxed) == table as *const _ as *mut _ {
            break table;
        }
    };
    let mut sum = new_table(num_threads, old_table);
    for entry in &old_table.entries[..] {
        sum += *entry as usize;
    }
    sum
}

fn main() {
    assert_eq!(grow_hashtable(1), 9);
    assert_eq!(grow_hashtable(0), 0);
}

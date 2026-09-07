/* hashbrown `reserve_rehash`: `Some(|ptr| ..)` handed to an `Option<unsafe fn(*mut u8)>`
   parameter after a closure argument.  Upstream checks the constructor as a call
   whose expected input is the fn pointer, so the closure is coerced to it; binding
   the variant's parameter to the closure itself leaves nothing to coerce. */
use std::ptr;

struct Table {
    count: usize,
}

impl Table {
    fn reserve_rehash_inner<F>(&mut self, hasher: &F, drop: Option<unsafe fn(*mut u8)>) -> usize
    where
        F: Fn(usize) -> u64,
    {
        self.count += 1;
        hasher(self.count) as usize + usize::from(drop.is_some())
    }
}

fn reserve<T>(table: &mut Table, hasher: impl Fn(usize) -> u64) -> usize {
    table.reserve_rehash_inner(
        &|index| hasher(index),
        if std::mem::needs_drop::<T>() {
            Some(|ptr| unsafe { ptr::drop_in_place(ptr as *mut T) })
        } else {
            None
        },
    )
}

fn main() {
    let mut table = Table { count: 0 };
    assert_eq!(reserve::<String>(&mut table, |i| i as u64 * 2), 3);
    assert_eq!(reserve::<u8>(&mut table, |i| i as u64), 2);
}

//@ check-pass
//@ compile-flags: -Znext-solver

struct LocalPointer;

impl LocalPointer {
    fn get(&self) -> *mut () {
        core::ptr::null_mut()
    }

    fn set(&self, _: *mut ()) {}
}

static CURRENT: LocalPointer = LocalPointer;

struct Thread;

impl Thread {
    unsafe fn from_raw(_: *const ()) -> Thread {
        Thread
    }
}

const DESTROYED: *mut () = core::ptr::without_provenance_mut(2);

fn inferred_mut_to_const<T>(address: usize) -> *const T {
    core::ptr::without_provenance_mut(address)
}

fn main() {
    let _: *const u8 = inferred_mut_to_const(0);
    let current = CURRENT.get();
    if current > DESTROYED {
        unsafe {
            CURRENT.set(DESTROYED);
            drop(Thread::from_raw(current));
        }
    }
}

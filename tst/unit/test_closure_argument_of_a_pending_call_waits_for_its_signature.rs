/* futures 0.1's `LocalKey::with`: `data.entry(key).or_insert_with(|| Box::new((self.__init)()))`
   into a `HashMap<TypeId, Box<dyn Opaque>>` - the closure's return is `Box<dyn Opaque>` from
   `or_insert_with`'s `F: FnOnce() -> V`, and the body's `Box<u32>` unsizes into it. */
use std::any::TypeId;
use std::cell::RefCell;
use std::collections::HashMap;

trait Opaque {}
impl Opaque for u32 {}

struct Task {
    map: RefCell<HashMap<TypeId, Box<dyn Opaque>>>,
}

fn init() -> u32 {
    5
}

fn with_task<F: FnOnce(&Task) -> R, R>(f: F) -> R {
    let t = Task { map: RefCell::new(HashMap::new()) };
    f(&t)
}

fn get<T: 'static>(key: TypeId) -> u32 {
    with_task(|task| {
        let raw_pointer = {
            let mut data = task.map.borrow_mut();
            let entry = data.entry(key).or_insert_with(|| Box::new(init()));
            &**entry as *const dyn Opaque as *const T as *const u32
        };
        unsafe { *raw_pointer }
    })
}

fn main() {
    assert_eq!(get::<u32>(TypeId::of::<u32>()), 5);
}

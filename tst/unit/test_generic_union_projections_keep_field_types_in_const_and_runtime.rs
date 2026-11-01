#[derive(Clone, Copy)]
struct Wrapper<T: Copy>(T);
union Storage<T: Copy> { direct: T, wrapped: Wrapper<T> }

const fn load<T: Copy>(storage: &Storage<T>) -> T { unsafe { storage.direct } }
fn store<T: Copy>(storage: &mut Storage<T>, value: T) { storage.direct = value; }
fn project<T: Copy>(storage: Storage<T>) -> T {
    unsafe { let Storage { wrapped: Wrapper(value) } = storage; value }
}

const ANSWER: u16 = load(&Storage { direct: 23 });

fn main() {
    let mut storage = Storage { wrapped: Wrapper(29u16) };
    assert_eq!(load(&storage), 29);
    store(&mut storage, 31);
    assert_eq!(project(storage), 31);
    assert_eq!(ANSWER, 23);
}

use std::mem::ManuallyDrop;
union Storage<T> { data: ManuallyDrop<T>, empty: () }
fn take<T>(mut storage: Storage<T>) -> T {
    unsafe { ManuallyDrop::take(&mut storage.data) }
}
fn main() {
    let mut storage = Storage { data: ManuallyDrop::new([41u32, 43, 47]) };
    unsafe {
        (*storage.data)[1] += 10;
        let values: &mut [u32; 3] = &mut *storage.data;
        values[2] += 20;
    }
    assert_eq!(take(storage), [41, 53, 67]);
    let owned = Storage { data: ManuallyDrop::new(Box::new(String::from("explicit"))) };
    assert_eq!(&**take(owned), "explicit");
}

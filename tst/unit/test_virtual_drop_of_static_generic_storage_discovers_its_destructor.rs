use std::marker::PhantomData;
use std::sync::atomic::{AtomicU32, Ordering};
static DROPS: AtomicU32 = AtomicU32::new(0);
struct Stored<T>(PhantomData<T>);
impl<T> Drop for Stored<T> { fn drop(&mut self) { DROPS.fetch_add(1, Ordering::Relaxed); } }
trait Marker { fn value(&self) -> u32 { 37 } }
impl<T> Marker for Stored<T> {}
static mut STORAGE: Stored<u32> = Stored(PhantomData);
fn main() {
    unsafe {
        let object = std::ptr::addr_of_mut!(STORAGE) as *mut dyn Marker;
        assert_eq!((*object).value(), 37);
        std::ptr::drop_in_place(object);
    }
    assert_eq!(DROPS.load(Ordering::Relaxed), 1);
}

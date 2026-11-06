use std::marker::PhantomData;
use std::mem::{align_of, size_of, ManuallyDrop};
use std::sync::atomic::{AtomicUsize, Ordering};
static DROPS: AtomicUsize = AtomicUsize::new(0);
#[repr(align(64))]
struct Z;
impl Drop for Z {
    fn drop(&mut self) { DROPS.fetch_add(1, Ordering::SeqCst); }
}
#[repr(C)]
struct Mixed { head: u8, marker: Z, tail: u16 }
enum Single { Only(Z) }
enum Unit { Only }
union Storage { marker: ManuallyDrop<Z>, unit: () }
#[inline(never)]
fn through(value: Z) -> Z { value }
fn add_one(value: u8) -> u8 { value + 1 }
fn invoke<F: Fn(u8) -> u8>(function: F) -> u8 {
    assert_eq!(size_of::<F>(), 0);
    function(7)
}
fn main() {
    assert_eq!(size_of::<Z>(), 0);
    assert_eq!(align_of::<Z>(), 64);
    assert_eq!(size_of::<[Z; 3]>(), 0);
    assert_eq!(align_of::<[Z; 3]>(), 64);
    {
        let values = [Z, Z, Z];
        let address = std::hint::black_box(&values[2]) as *const Z as usize;
        assert_eq!(address % 64, 0);
    }
    assert_eq!(DROPS.load(Ordering::SeqCst), 3);
    {
        let mut pair = (9u8, Z);
        std::hint::black_box(&mut pair.1);
        let item = Mixed { head: 2, marker: Z, tail: 3 };
        assert_eq!(std::mem::offset_of!(Mixed, marker), 64);
        assert_eq!((item.head, item.tail), (2, 3));
    }
    assert_eq!(DROPS.load(Ordering::SeqCst), 5);
    drop(through(Z));
    assert_eq!(DROPS.load(Ordering::SeqCst), 6);
    match std::hint::black_box(Single::Only(Z)) { Single::Only(value) => drop(value) }
    assert_eq!(DROPS.load(Ordering::SeqCst), 7);
    let storage = Storage { marker: ManuallyDrop::new(Z) };
    drop(unsafe { ManuallyDrop::into_inner(storage.marker) });
    assert_eq!(DROPS.load(Ordering::SeqCst), 8);
    drop((PhantomData::<Z>, Z));
    assert_eq!(DROPS.load(Ordering::SeqCst), 9);
    assert_eq!(size_of::<Unit>(), 0);
    match std::hint::black_box(Unit::Only) { Unit::Only => {} }
    assert_eq!(invoke(add_one), 8);
}

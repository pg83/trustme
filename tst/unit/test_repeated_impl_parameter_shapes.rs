trait Uniform { fn width(&self) -> usize; }
impl<T> Uniform for (T, T) {
    fn width(&self) -> usize { std::mem::size_of::<T>() }
}

trait Value { fn value(&self) -> u8; }
impl Value for u8 { fn value(&self) -> u8 { *self } }

fn borrowed(value: &u8) -> &u8 { value }
unsafe fn unsafe_borrowed(value: &u8) -> &u8 { value }
extern "C" fn foreign(value: u16) -> u16 { value + 1 }

fn main() {
    assert_eq!(([1u8, 2], [3u8, 4]).width(), 2);
    assert_eq!(((1u8, 2u16), (3u8, 4u16)).width(), 4);
    let a: &[u8] = &[1, 2];
    let b: &[u8] = &[3];
    assert_eq!((a, b).width(), std::mem::size_of::<&[u8]>());
    let mut a = 1u16;
    let mut b = 2u16;
    assert_eq!((&mut a, &mut b).width(), std::mem::size_of::<&mut u16>());
    let pointers = (&a as *const u16, &b as *const u16);
    assert_eq!(pointers.width(), std::mem::size_of::<*const u16>());
    let f: for<'a> fn(&'a u8) -> &'a u8 = borrowed;
    assert_eq!((f, f).width(), std::mem::size_of_val(&f));
    assert_eq!(*f(&9), 9);
    let f: unsafe fn(&u8) -> &u8 = unsafe_borrowed;
    assert_eq!((f, f).width(), std::mem::size_of_val(&f));
    let f: extern "C" fn(u16) -> u16 = foreign;
    assert_eq!((f, f).width(), std::mem::size_of_val(&f));
    assert_eq!(f(3), 4);
    let a: &(dyn Value + Send) = &5u8;
    let b: &(dyn Value + Send) = &6u8;
    assert_eq!((a, b).width(), std::mem::size_of_val(&a));
    assert_eq!(a.value() + b.value(), 11);
    let a = Some(Ok::<u8, u16>(1));
    let b = Some(Err::<u8, u16>(2));
    assert_eq!((a, b).width(), std::mem::size_of_val(&a));
}

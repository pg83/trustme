#![feature(trait_alias, trivial_bounds)]
trait Mark {}
impl<T: ?Sized> Mark for T {}
trait Object<T> {}
trait Shapes<T, U> = Mark where
    u8: Mark, u16: Mark, [T]: Mark, [U]: Mark,
    [T; 2]: Mark, [T; 3]: Mark,
    (T,): Mark, (U,): Mark,
    *const T: Mark, *mut U: Mark,
    &'static u8: Mark, &'static mut u16: Mark,
    fn(T) -> T: Mark, fn(T) -> U: Mark, unsafe fn(T) -> T: Mark,
    dyn Object<T>: Mark, dyn Object<U>: Mark, (dyn Object<T> + Send): Mark;
fn retain<T: Shapes<u8, u16>>(value: T) -> T { value }
fn main() { assert_eq!(retain(37u32), 37); }

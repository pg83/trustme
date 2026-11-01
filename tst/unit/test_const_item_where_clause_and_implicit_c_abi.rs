#![feature(generic_const_items)]

const WIDTH<T>: usize = core::mem::size_of::<T>() where T: Copy;

const unsafe extern fn identity(value: u16) -> u16 { value }
const VALUE: u16 = unsafe { identity(23) };

fn main() {
    assert_eq!(WIDTH::<u64>, 8);
    assert_eq!(WIDTH::<[u16; 3]>, 6);
    assert_eq!(VALUE, 23);
    let function: unsafe extern fn(u16) -> u16 = identity;
    assert_eq!(unsafe { function(29) }, 29);
}

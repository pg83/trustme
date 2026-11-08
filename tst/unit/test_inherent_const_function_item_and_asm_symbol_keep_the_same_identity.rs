//@ compile-flags: -O

use core::marker::PhantomData;

struct Factory<T>(PhantomData<T>);
impl<T> Factory<T> {
    extern "C" fn number<const N: usize>() -> usize { N }
}

trait Identity {
    type Value;
    fn identity(self) -> Self::Value;
}
impl<T> Identity for T {
    type Value = T;
    fn identity(self) -> T { self }
}
fn through_alias<T: Identity>(value: T) -> T::Value { value.identity() }

#[inline(always)]
fn symbol<T, const N: usize>() -> usize {
    let address;
    unsafe {
        core::arch::asm!(
            "lea {address}, [rip + {target}]",
            address = lateout(reg) address,
            target = sym Factory::<T>::number::<N>,
            options(nomem, nostack, preserves_flags),
        );
    }
    address
}

fn main() {
    let direct = through_alias(Factory::<u16>::number::<37>);
    let other = through_alias(Factory::<u32>::number::<61>);
    let address = symbol::<u16, 37>();
    let other_address = symbol::<u32, 61>();
    assert_eq!(address, direct as extern "C" fn() -> usize as usize);
    assert_eq!(other_address, other as extern "C" fn() -> usize as usize);
    let restored = unsafe { core::mem::transmute::<usize, extern "C" fn() -> usize>(address) };
    assert_eq!(restored(), direct());
    assert_eq!(direct(), 37);
    assert_eq!(other(), 61);
}

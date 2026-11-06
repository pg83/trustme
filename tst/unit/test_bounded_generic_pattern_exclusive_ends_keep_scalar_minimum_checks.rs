//@ compile-fail: generic
#![feature(pattern_types, pattern_type_macro, const_trait_impl, pattern_type_range_trait, generic_pattern_types, generic_const_exprs)]
type Byte = std::pat::pattern_type!(u8 is ..1);
type Signed = std::pat::pattern_type!(i8 is i8::MIN..-127);
type Scalar = std::pat::pattern_type!(char is '\0'..='\0');
type Wide = std::pat::pattern_type!(u128 is ..=0);
type Present = std::pat::pattern_type!(*const u8 is !null);
#[cfg(unsigned_min)] type Bad = std::pat::pattern_type!(u128 is ..0);
#[cfg(signed_min)] type Bad = std::pat::pattern_type!(i128 is ..i128::MIN);
#[cfg(char_min)] type Bad = std::pat::pattern_type!(char is ..'\0');
const fn generic_size<const END: u8>() -> usize where [(); END as usize]: {
    std::mem::size_of::<Option<std::pat::pattern_type!(u8 is ..END)>>()
}
#[cfg(const_generic_min)] const BAD_SIZE: usize = generic_size::<0>();
fn read_byte(value: &Byte) -> u8 { unsafe { std::ptr::read(value as *const Byte as *const u8) } }
fn keep_pointer(value: Present) -> Present { value }
fn main() {
    #[cfg(any(unsigned_min, signed_min, char_min))] { let _ = std::mem::size_of::<Option<Bad>>(); }
    #[cfg(generic_min)] { let _ = generic_size::<0>(); }
    #[cfg(const_generic_min)] { let _ = BAD_SIZE; }
    assert_eq!(std::mem::size_of::<Byte>(), 1);
    assert_eq!(std::mem::size_of::<Signed>(), 1);
    assert_eq!(std::mem::size_of::<Scalar>(), 4);
    assert_eq!(std::mem::size_of::<Wide>(), 16);
    assert_eq!(generic_size::<2>(), 1);
    let byte: Byte = unsafe { std::mem::transmute(0u8) };
    assert_eq!(read_byte(&byte), 0);
    let data = 7u8;
    let pointer: Present = unsafe { std::mem::transmute(&data as *const u8) };
    let raw: *const u8 = unsafe { std::mem::transmute(keep_pointer(pointer)) };
    assert_eq!(unsafe { *raw }, 7);
}

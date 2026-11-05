//@ reject-flags: --cfg=bad_question || --cfg=bad_question => error || error
//@ reject-flags: --cfg=bad_lifetime || --cfg=bad_lifetime => error || error
//@ reject-flags: --cfg=bad_pointer || --cfg=bad_pointer => error || error
//@ reject-flags: --cfg=bad_array || --cfg=bad_array => error || error
//@ reject-flags: --cfg=bad_type_keyword || --cfg=bad_type_keyword => error || error
//@ reject-flags: --cfg=bad_field_overflow || --cfg=bad_field_overflow => error || error
//@ reject-flags: --cfg=bad_field_token || --cfg=bad_field_token => error || error
//@ reject-flags: --cfg=bad_do_keyword || --cfg=bad_do_keyword => error || error
#![allow(unused_macros)]

macro_rules! malformed {
    (question) => { type Broken = &?Sized; };
    (lifetime) => { type Broken = &'static 'static; };
    (pointer) => { type Broken = *u8; };
    (array) => { type Broken = [u8, u8]; };
    (type_keyword) => { type Broken = return; };
    (field_overflow) => { fn broken() { let _ = (0u8,).18446744073709551616.0; } };
    (field_token) => { fn broken() { let _ = (1u8).+; } };
    (do_keyword) => { fn broken() { let _ = do impossible; } };
}
#[cfg(bad_question)]
malformed!(question);
#[cfg(bad_lifetime)]
malformed!(lifetime);
#[cfg(bad_pointer)]
malformed!(pointer);
#[cfg(bad_array)]
malformed!(array);
#[cfg(bad_type_keyword)]
malformed!(type_keyword);
#[cfg(bad_field_overflow)]
malformed!(field_overflow);
#[cfg(bad_field_token)]
malformed!(field_token);
#[cfg(bad_do_keyword)]
malformed!(do_keyword);

type Bytes = [u8; 3];
type Borrowed<'a> = &'a Bytes;
type Pointer = *const u8;
fn main() {
    let values: Bytes = [3, 5, 8];
    let borrowed: Borrowed<'_> = &values;
    let pointer: Pointer = borrowed.as_ptr();
    assert_eq!(std::mem::size_of::<Bytes>(), 3);
    assert_eq!(unsafe { *pointer.add(2) }, 8);
}

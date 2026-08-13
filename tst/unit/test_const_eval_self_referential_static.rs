struct StaticValue(&'static u32, u32);

const fn second(value: &StaticValue) -> &u32 {
    &value.1
}

static VALUE: StaticValue = StaticValue(second(&VALUE), 37);

struct Borrowed {
    value: &'static (),
}

static BORROWED: &Borrowed = &Borrowed { value: &() };
static BORROWED_AGAIN: &Borrowed = &BORROWED;

static NUMBER: usize = 41;
static NUMBER_REFERENCE: &usize = &NUMBER;
static NUMBER_COPY: usize = *NUMBER_REFERENCE;

const NON_NULL: core::ptr::NonNull<u8> = unsafe {
    core::ptr::NonNull::new_unchecked((&42u8 as *const u8).cast_mut())
};
const NON_NULL_COPY: u8 = *unsafe { NON_NULL.as_ref() };

const ARRAY: [u8; 1] = [b'x'];
const NESTED_ARRAY: &&&&[u8; 1] = &&&&ARRAY;
const ARRAY_COPY: u8 = NESTED_ARRAY[0];

static mut BYTE: u8 = 0;
static mut BYTE_POINTER: *mut u8 = core::ptr::addr_of_mut!(BYTE);
static mut DEREFERENCED_BYTE_POINTER: *mut u8 = unsafe {
    core::ptr::addr_of_mut!(*BYTE_POINTER)
};

fn main() {
    assert_eq!(*VALUE.0, 37);
    assert_eq!(BORROWED_AGAIN.value, &());
    assert_eq!(NUMBER_COPY, 41);
    assert_eq!(NON_NULL_COPY, 42);
    assert_eq!(ARRAY_COPY, b'x');
    assert_eq!(unsafe { DEREFERENCED_BYTE_POINTER }, core::ptr::addr_of_mut!(BYTE));
}

//@ compile-variant: -O
#[repr(i16)]
enum Tag { Low = -23, High = 71 }

fn main() {
    assert_eq!(0x173bu16 as u8, 0x3b);
    assert_eq!(-19i16 as i32, -19);
    assert_eq!(true as u8, 1);
    assert_eq!(false as i64, 0);
    assert_eq!(b'Q' as char, 'Q');
    assert_eq!('\u{10000}' as u32, 0x10000);
    assert_eq!(3.75f64 as i32, 3);
    assert_eq!(17u16 as f64, 17.0);
    assert_eq!(Tag::Low as i32, -23);
    assert_eq!(Tag::High as u8, 71);

    let mut bytes = [13u8, 29, 43];
    let array = &mut bytes as *mut [u8; 3];
    let slice = array as *mut [u8];
    let data = slice as *mut u8;
    assert_eq!(data, bytes.as_mut_ptr());
    assert_eq!(slice.len(), 3);
    let shared = data as *const u8;
    assert_eq!(shared as usize, bytes.as_ptr() as usize);
    assert_eq!(0usize as *const u8, core::ptr::null());
    assert_eq!(bytes[0], 13);
}

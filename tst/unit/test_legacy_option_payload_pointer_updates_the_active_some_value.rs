#![feature(intrinsics)]
#![allow(internal_features)]

extern "rust-intrinsic" {
    fn option_payload_ptr<T>(value: *mut Option<T>) -> *mut T;
}

fn main() {
    let mut scalar = Some(47u16);
    let expected = scalar.as_mut().unwrap() as *mut u16;
    let actual = unsafe {
        option_payload_ptr(std::hint::black_box(&mut scalar as *mut Option<u16>))
    };
    assert_eq!(actual, expected);
    unsafe { *actual = 61; }
    assert_eq!(scalar, Some(61));

    let mut pair = Some((67u64, 71u8));
    let actual = unsafe {
        option_payload_ptr(std::hint::black_box(&mut pair as *mut Option<(u64, u8)>))
    };
    assert_eq!(actual, pair.as_mut().unwrap() as *mut (u64, u8));
    unsafe {
        (*actual).0 = 83;
        (*actual).1 = 89;
    }
    assert_eq!(pair, Some((83, 89)));
}

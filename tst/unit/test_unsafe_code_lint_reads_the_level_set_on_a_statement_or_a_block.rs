//@ run-pass
// Under `#![deny(unsafe_code)]`, `#[allow(unsafe_code)]` on a `let`, on a block
// expression or on a match arm admits the `unsafe` block inside: upstream builds
// its lint levels over every attributed HIR node, not items alone.  rustix's
// `cstr!` expands to `#[allow(unsafe_code, unused_unsafe)] { unsafe { .. } }`.
#![deny(unsafe_code)]

macro_rules! cstr {
    ($str:literal) => {{
        #[allow(unsafe_code, unused_unsafe)]
        {
            unsafe { ::core::ffi::CStr::from_bytes_with_nul_unchecked(::core::concat!($str, "\0").as_bytes()) }
        }
    }};
}

fn read(p: *const u8) -> u8 {
    #[allow(unsafe_code)]
    let value = unsafe { *p };
    value
}

fn read_block(p: *const u8) -> u8 {
    #[allow(unsafe_code)]
    {
        unsafe { *p }
    }
}

fn read_arm(p: Option<*const u8>) -> u8 {
    match p {
        #[allow(unsafe_code)]
        Some(p) => unsafe { *p },
        None => 0,
    }
}

fn main() {
    let x = 7u8;
    assert_eq!(read(&x), 7);
    assert_eq!(read_block(&x), 7);
    assert_eq!(read_arm(Some(&x)), 7);
    assert_eq!(read_arm(None), 0);
    assert_eq!(cstr!("/dev/null").to_bytes(), b"/dev/null");
}

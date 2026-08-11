// { dg-additional-options "-w" }

const fn slice_from_raw_parts<T>(data: *const T, len: usize) -> *const [T] {
    std::ptr::slice_from_raw_parts(data, len)
}

fn gccrs_main() -> i32 {
    let a = 123;
    let b: *const i32 = &a;
    let c = slice_from_raw_parts(b, 1);
    assert_eq!(unsafe { (&*c)[0] }, 123);

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }

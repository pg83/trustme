// Rust's C ABI passes nothing for a zero-sized argument, so the C function on
// the other side reads the argument that follows it. Nothing is passed for the
// stand-in the generated C++ gives such a struct either.

#[repr(C)]
struct Empty;

#[repr(C)]
struct Pair {
    a: i32,
    b: i32,
}

#[cfg(extern_c_zst_argument)]
#[link(name = "extern_c_zst_argument", kind = "static")]
extern "C" {
    fn extern_c_zst_between(first: Pair, gap: Empty, second: Pair) -> i32;
    fn extern_c_zst_leading(gap: Empty, value: i32) -> i32;
    fn extern_c_zst_only(gap: Empty) -> i32;
}

fn main() {
    #[cfg(extern_c_zst_argument)]
    unsafe {
        let first = Pair { a: 1, b: 2 };
        let second = Pair { a: 3, b: 4 };
        assert_eq!(extern_c_zst_between(first, Empty, second), 1234);
        assert_eq!(extern_c_zst_leading(Empty, 9), 9);
        assert_eq!(extern_c_zst_only(Empty), 17);
    }
}

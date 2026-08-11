#![feature(intrinsics, staged_api)]
#![feature(lang_items)]
mod mem {
    extern "rust-intrinsic" {
        #[rustc_const_stable(feature = "const_size_of", since = "1.40.0")]
        pub fn size_of<T>() -> usize;
    }
}

struct Foo<T>;

impl<T> Foo<T> {
    const MAGIC: usize = mem::size_of::<T>();
}

fn gccrs_main() -> i32 {
    let sz = Foo::<u16>::MAGIC;
    sz as i32 - 2
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }

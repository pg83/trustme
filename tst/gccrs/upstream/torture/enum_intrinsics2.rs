
#![feature(intrinsics)]

#![feature(lang_items)]
enum BookFormat {
    Paperback,
    Hardback,
    Ebook,
}

mod core {
    mod intrinsics {
        extern "rust-intrinsic" {
            #[rustc_const_unstable(feature = "variant_count", issue = "73662")]
            pub fn variant_count<T>() -> usize;
        }
    }
}

pub fn gccrs_main() -> i32 {
    let count = core::intrinsics::variant_count::<BookFormat>();

    (count as i32) - 3
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }

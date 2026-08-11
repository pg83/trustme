// { dg-additional-options "-w" }

#![feature(intrinsics, lang_items)]

extern "rust-intrinsic" {
    fn transmute<T, U>(value: T) -> U;
}

struct WrapI {
    inner: i32,
}

struct WrapF {
    inner: f32,
}

fn gccrs_main() -> i32 {
    let f = 15.4f32;
    let f_wrap = WrapF { inner: f };

    let fst = unsafe { transmute::<f32, i32>(f) };
    let snd = unsafe { transmute::<WrapF, WrapI>(f_wrap) };

    fst - snd.inner
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }

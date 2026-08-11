// { dg-additional-options "-w" }

#[repr(transparent)]
struct WrapI {
    inner: i32,
}

#[repr(transparent)]
struct WrapF {
    inner: f32,
}

fn gccrs_main() -> i32 {
    let value = 15.4f32;
    let wrapped = WrapF { inner: value };
    let first = value.to_bits() as i32;
    let second = unsafe { std::mem::transmute::<WrapF, WrapI>(wrapped) };
    first - second.inner
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}

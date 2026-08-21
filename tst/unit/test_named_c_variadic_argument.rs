#![feature(c_variadic)]

pub unsafe extern "C" fn forward(_anchor: usize, mut arguments: ...) {
    let _arguments = arguments.as_va_list();
}

fn main() {
    let _forward = forward as unsafe extern "C" fn(usize, ...);
}

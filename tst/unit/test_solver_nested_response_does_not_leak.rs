//@ check-pass
//@ compile-flags: -Znext-solver

fn into_string(value: Box<str>) -> String {
    let slice = Box::<[u8]>::from(value);
    unsafe { String::from_utf8_unchecked(slice.into_vec()) }
}

fn main() {}


pub fn foo(a: &[u32]) {
    match a {
        [first, ..] => {}
        [.., last] => {}
        _ => {}
    }
}

#[cfg(any())]
pub fn foo([.., ..]: ()) {}

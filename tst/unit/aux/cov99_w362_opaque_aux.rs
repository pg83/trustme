pub struct Token;

impl Drop for Token {
    fn drop(&mut self) {}
}

const fn hidden() -> impl Sized {
    Token
}

#[inline]
pub const fn retained(fail: bool) -> impl Sized {
    let value = hidden();
    if fail {
        panic!("w362 checkpoint");
    }
    value
}

pub const LEN: usize = {
    core::mem::forget(retained(false));
    1
};

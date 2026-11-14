//@ compile-fail: Failed to find an impl of
//@ compile-flags: --emit=metadata -Cpanic=abort

async fn accepts_bool(_: bool) {}

fn require_byte<F: std::ops::AsyncFnOnce(u8)>(_: F) {}

fn main() {
    require_byte(accepts_bool);
}

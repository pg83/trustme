//@ compile-fail: refutable pattern
async fn selected((17, 1.5, "warm", b"seed"): (u8, f32, &str, &[u8; 4])) -> u32 {
    103
}
fn main() {
    let _future = selected((17, 1.5, "warm", b"seed"));
}

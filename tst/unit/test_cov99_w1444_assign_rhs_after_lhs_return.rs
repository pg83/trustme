//@ compile-only
//@ compile-flags: -Cpanic=abort

struct Dropped;

impl Drop for Dropped {
    fn drop(&mut self) {}
}

fn main() {
    let mut dst = Dropped;
    *{
        return;
        &mut dst
    } = Dropped;
}

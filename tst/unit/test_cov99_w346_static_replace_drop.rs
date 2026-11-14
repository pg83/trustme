//@ edition: 2021
//@ compile-fail: cannot be evaluated at compile-time
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Dropped(u8);
impl Drop for Dropped {
    fn drop(&mut self) {}
}
static mut VALUE: Dropped = Dropped(1);
const _: () = unsafe {
    VALUE = Dropped(2);
};
fn main() {}

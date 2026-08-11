
#![feature(auto_traits)]

unsafe auto trait Send {}
unsafe auto trait Sync {}

fn take_send(_: &dyn Send) {}
fn take_sync(_: &dyn Sync) {}

fn main() {
    let a = 0i32;

    take_send(&a);
    take_sync(&a);
}

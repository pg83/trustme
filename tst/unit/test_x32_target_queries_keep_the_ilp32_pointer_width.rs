//@ stdout-flags: --target=x86_64-unknown-linux-gnux32 --print=cfg => target_pointer_width="32"
fn main() {
    let values = [439u32, 443];
    assert_eq!(unsafe { values.as_ptr().add(1).read() }, 443);
    assert_eq!(core::mem::size_of::<usize>(), core::mem::size_of::<*const u32>());
}

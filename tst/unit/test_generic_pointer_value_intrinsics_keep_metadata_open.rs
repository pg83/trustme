#![feature(core_intrinsics)]
fn pointer_width<T:?Sized>(pointer:&*const T)->usize {
    unsafe { std::intrinsics::size_of_val(pointer) }
}
fn pointer_align<T:?Sized>(pointer:&*const T)->usize {
    unsafe { std::intrinsics::align_of_val(pointer) }
}
fn main() {
    let value=17u8;
    let array=[19u8;3];
    let thin=&value as *const u8;
    let wide=&array[..] as *const [u8];
    let text="word" as *const str;
    let word=std::mem::size_of::<usize>();
    assert_eq!(pointer_width(&thin),word);
    assert_eq!(pointer_width(&wide),word*2);
    assert_eq!(pointer_width(&text),word*2);
    assert_eq!(pointer_align(&thin),std::mem::align_of::<usize>());
    assert_eq!(pointer_align(&wide),std::mem::align_of::<usize>());
}

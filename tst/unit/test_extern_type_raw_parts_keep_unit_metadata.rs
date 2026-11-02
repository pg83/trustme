#![feature(extern_types, ptr_metadata)]

unsafe extern "C" { type Foreign; }

fn main() {
    let shared = std::ptr::from_raw_parts::<Foreign>(37usize as *const (), ());
    let unique = std::ptr::from_raw_parts_mut::<Foreign>(53usize as *mut (), ());
    assert_eq!(std::ptr::metadata(shared), ());
    assert_eq!(std::ptr::metadata(unique), ());
    assert_eq!(shared as *const () as usize, 37);
    assert_eq!(unique as *mut () as usize, 53);
    assert_eq!(std::mem::size_of_val(&shared), std::mem::size_of::<usize>());
}

#![feature(core_intrinsics, const_heap)]

static DATA: &[u8; 65536] = unsafe {
    let data = core::intrinsics::const_allocate(65536, 64);
    (data as *mut [u8; 65536]).write([0x5a; 65536]);
    &*(core::intrinsics::const_make_global(data) as *const [u8; 65536])
};

fn main() {
    assert_eq!(DATA.len(), 65536);
    assert_eq!(DATA[0], 0x5a);
    assert_eq!(DATA[65535], 0x5a);
    assert_eq!(DATA.as_ptr() as usize % 64, 0);
}

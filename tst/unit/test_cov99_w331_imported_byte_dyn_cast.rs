//@ aux-build: cov99_w331_byte_dyn_cast.rs
//@ env: TRUSTME_DUMP=mir

const VALUE: u8 = unsafe {
    *(cov99_w331_byte_dyn_cast::object::<()>() as *const u8)
};

fn main() {
    assert_eq!(VALUE, b'Q');
}

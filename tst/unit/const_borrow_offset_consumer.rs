extern crate const_borrow_offset_producer as producer;

fn main() {
    assert_eq!(*producer::field::<u16>(), 0x1234_5678);
}

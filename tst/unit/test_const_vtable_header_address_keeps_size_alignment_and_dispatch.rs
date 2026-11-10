//@ compile-flags: -O

trait Value {
    fn read(&self) -> u32;
}

#[repr(align(32))]
struct Source(u32);

impl Value for Source {
    fn read(&self) -> u32 {
        self.0
    }
}

#[repr(C)]
struct Header {
    drop: *const (),
    size: usize,
    align: usize,
}

static SOURCE: Source = Source(59);
const OBJECT: &dyn Value = &SOURCE;
const HEADER: *const Header = unsafe {
    std::mem::transmute::<&dyn Value, (*const (), *const Header)>(OBJECT).1
};

fn main() {
    assert_eq!(unsafe { (*HEADER).size }, std::mem::size_of::<Source>());
    assert_eq!(unsafe { (*HEADER).align }, std::mem::align_of::<Source>());
    assert_eq!(OBJECT.read(), 59);
}

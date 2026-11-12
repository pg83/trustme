macro_rules! inspect_path {
    ($path:path) => {
        (core::mem::size_of::<$path>(), <$path>::VALUE)
    };
}

macro_rules! forward_type {
    ($type:ty) => {
        inspect_path!($type)
    };
}

struct Cell<T>(T);

impl<T> Cell<T> {
    const VALUE: u16 = 887;

    fn inspect_self() -> (usize, u16) {
        forward_type!(Self)
    }
}

mod lane {
    pub struct Record(pub u16);

    impl Record {
        pub const VALUE: u16 = 883;
    }
}

fn main() {
    assert_eq!(forward_type!(lane::Record), (2, 883));
    assert_eq!(forward_type!(Cell<u8>), (1, 887));
    assert_eq!(Cell::<u64>::inspect_self(), (8, 887));
}

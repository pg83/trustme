mod storage {
    #[derive(Clone, Debug, PartialEq)]
    pub struct Buffer {
        pub used: usize,
        pub bytes: [u8; Self::WIDTH],
    }

    impl Default for Buffer {
        fn default() -> Self {
            Self { used: 0, bytes: [0; Self::WIDTH] }
        }
    }

    impl Buffer {
        pub const WIDTH: usize = 4;
    }
}

fn main() {
    let mut value = storage::Buffer::default();
    value.bytes[2] = 13;
    value.used = 1;
    assert_eq!(value, value.clone());
    assert_eq!(value.bytes, [0, 0, 13, 0]);
    assert_eq!(format!("{:?}", value), "Buffer { used: 1, bytes: [0, 0, 13, 0] }");
}

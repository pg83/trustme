// bytes-varint 1.1 beside bytes 1.12: `TryGetFixedSupport: Buf` declares
// `try_get_i128_le`, and `Buf` gained a method of the same name. Only the
// subtrait is imported, and a trait in scope offers its own items alone, so
// the call is not ambiguous.
mod buf {
    pub trait Buf {
        fn get(&mut self) -> u8;

        fn try_get(&mut self) -> Result<u8, ()> {
            Err(())
        }
    }

    impl Buf for &[u8] {
        fn get(&mut self) -> u8 {
            self[0]
        }
    }

    pub trait TryGet: Buf {
        fn try_get(&mut self) -> Option<u8> {
            Some(self.get())
        }
    }

    impl<T: Buf> TryGet for T {}
}

mod user {
    use crate::buf::TryGet;

    pub fn first_is(bytes: &[u8], expected: u8) -> bool {
        let mut buf: &[u8] = bytes;
        let got = buf.try_get();
        got == Some(expected)
    }
}

fn main() {
    assert!(user::first_is(&[7, 8], 7));
}

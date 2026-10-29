pub trait Read {
    const WIDTH: usize;

    fn read() -> usize {
        Self::WIDTH + 1
    }
}

pub struct Token;

impl Read for Token {
    const WIDTH: usize = 19;
}

impl Token {
    pub const fn width() -> usize {
        <Self as Read>::WIDTH + 2
    }
}

pub mod nested {
    pub static BYTES: [u8; 3] = [2, 4, 8];

    pub const fn sum() -> usize {
        super::Token::width() + std::mem::size_of::<[u8; 5]>()
    }
}

pub struct Table<const N: usize>;

impl<const N: usize> Table<N> {
    pub const VALUES: [u8; N] = [3; N];

    pub const fn values() -> &'static [u8; N] {
        &Self::VALUES
    }
}

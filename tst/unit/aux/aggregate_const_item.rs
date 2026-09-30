pub struct Big {
    pub a: u64,
    pub b: u64,
    pub c: u64,
}

impl Big {
    pub const NEW: Self = Big { a: 1, b: 2, c: 3 };

    pub fn fresh<T>() -> Self {
        Self::NEW
    }
}

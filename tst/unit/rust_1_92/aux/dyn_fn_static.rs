pub struct Kem {
    pub generate: &'static (dyn Fn() -> u8 + Send + Sync),
}

fn generate_secret() -> u8 {
    7
}

static KEM: &Kem = &Kem { generate: &generate_secret };

pub struct Suite<const N: usize>;

impl<const N: usize> Suite<N> {
    pub fn kem(&self) -> &'static Kem {
        KEM
    }
}

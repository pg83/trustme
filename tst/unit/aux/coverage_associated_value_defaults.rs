pub trait Supply {
    const VALUE: u16 = 1327;
}

pub struct ExternalOverride;

impl Supply for ExternalOverride {
    const VALUE: u16 = 1329;
}

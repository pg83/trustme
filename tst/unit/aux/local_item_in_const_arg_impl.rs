pub const LEN: usize = 4;

pub trait ShortMac<const N: usize> {
    fn validate(&self, mac: &[u8; N]) -> bool;
}

pub struct Key<'a>(pub &'a [u8]);

impl<'a> ShortMac<LEN> for Key<'a> {
    fn validate(&self, mac: &[u8; LEN]) -> bool {
        use std::convert::AsRef as _;
        let mac: &[u8] = mac.as_ref();
        mac.starts_with(self.0)
    }
}
